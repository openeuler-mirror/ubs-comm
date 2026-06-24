#!/usr/bin/env python3
"""
UBSOCKET 性能分析工具 – 重构版
功能：从 UBSOCKET 日志中提取 Write/Epoll/Read 各阶段时间戳
"""

import re, sys, math, argparse, csv

# ========== 常量定义 ==========
EPOLL_ROWS_PER_ROUND_CLIENT = 12
EPOLL_ROWS_PER_ROUND_SERVER = 11
READ_ROWS_PER_ROUND = 7
CLIENT_WRITE_BASE = 1
SERVER_WRITE_BASE = 1
CLIENT_EPOLL_BASE = 5
SERVER_EPOLL_BASE = 4

TYPE_WRITE_START = 26
TYPE_WRITEV = 2
TYPE_POST = 19
TYPE_EPOLL_REARM = 9
TYPE_RECV_POLL = 10
TYPE_BUFF_ALLOC = 11
TYPE_UMPQ_POST = 12
TYPE_BUFFER_ENQUEUE = 14
TYPE_EPOLL_END = 13
TYPE_READ_DEQUEUE = 6
TYPE_POLL_QBUF = 5
TYPE_DATASET = 7
TYPE_READV_END = 3

# ========== 解析函数 ==========
def parse_start_end(line):
    s = re.search(r'start_timestamp:\s*(\d+)', line)
    e = re.search(r'end_timestamp:\s*(\d+)', line)
    return (int(s.group(1)) if s else None,
            int(e.group(1)) if e else None)

def parse_offset(line):
    m = re.search(r'offset:\s*(\d+)', line)
    return int(m.group(1)) if m else None

def parse_seq(line):
    m = re.search(r'seq:\s*(\d+)', line)
    return int(m.group(1)) if m else None

def parse_type(line):
    m = re.search(r'type:\s*(\d+)', line)
    return int(m.group(1)) if m else None

def parse_category(line):
    m = re.search(r'\[(Write|Epoll|Read)\]', line)
    return m.group(1) if m else None

# ========== 过滤函数 ==========
def filter_lines_by_seq(lines, start_seq, end_seq, include_zero=False):
    filtered = []
    for line in lines:
        seq = parse_seq(line)
        if seq is None:
            continue
        if include_zero and seq == 0:
            filtered.append(line)
        elif start_seq <= seq <= end_seq:
            filtered.append(line)
    return filtered

def filter_epoll_by_range(epoll_lines, start_seq, end_seq):
    if not epoll_lines:
        return []
    # 找到第一个 seq=start_seq 的行
    pos_start = next((i for i, line in enumerate(epoll_lines)
                      if parse_seq(line) == start_seq), None)
    if pos_start is None:
        return []
    # 向前找最近的 type=9
    begin = pos_start
    for i in range(pos_start, -1, -1):
        if parse_type(epoll_lines[i]) == TYPE_EPOLL_REARM:
            begin = i
            break
    # 向后找最后一个 seq=end_seq 且 type=14 的行
    end14 = pos_start
    for i in range(pos_start, len(epoll_lines)):
        if parse_seq(epoll_lines[i]) == end_seq and \
           parse_type(epoll_lines[i]) == TYPE_BUFFER_ENQUEUE:
            end14 = i
    # 从 end14+1 开始找第一个 type=13 作为终点
    end = end14
    for i in range(end14 + 1, len(epoll_lines)):
        if parse_type(epoll_lines[i]) == TYPE_EPOLL_END:
            end = i
            break
    return epoll_lines[begin:end+1]

# ========== 校验函数 ==========
def validate_header_seq(write_lines, start_seq, side_name):
    header_type = TYPE_WRITE_START if side_name == 'Client' else TYPE_WRITEV
    for line in write_lines:
        if parse_seq(line) == start_seq and parse_type(line) == header_type:
            return True
    available = sorted({parse_seq(line) for line in write_lines
                        if parse_type(line) == header_type and parse_seq(line) is not None})
    display = available[:100]
    if len(available) > 100:
        display.append('...')
    print(f"[ERROR] {side_name} 侧输入的 seq={start_seq} 不是消息头。\n"
          f"       消息头 type 为 {header_type}。\n"
          f"       可用的消息头 seq 值（前100个）：{display}")
    return False

# ========== 类别提取函数 ==========
def extract_write(lines):
    data = {}
    for line in lines:
        typ = parse_type(line)
        if typ == TYPE_WRITE_START:
            data['brpc_start'], _ = parse_start_end(line)
        elif typ == TYPE_WRITEV:
            data['writev_start'], _ = parse_start_end(line)
        elif typ == TYPE_POST:
            s, e = parse_start_end(line)
            data['post_before'] = s
            data['post_after'] = e
    return data

def extract_epoll_rounds(lines):
    rounds = []
    current = None
    for line in lines:
        typ = parse_type(line)
        if typ is None:
            continue
        if typ == TYPE_EPOLL_REARM:
            if current:
                rounds.append(current)
            current = {
                'offset9': parse_offset(line),
                'type9_start': parse_start_end(line)[0],
                'type9_end': parse_start_end(line)[1],
                'type10_start': None, 'type10_end': None,
                'type11_start': None, 'type11_end': None,
                'type12_start': None, 'type12_end': None,
                'type14_first': None, 'type14_last': None,
                'type13_start': None,
            }
        elif current:
            if typ == TYPE_RECV_POLL:
                current['type10_start'], current['type10_end'] = parse_start_end(line)
            elif typ == TYPE_BUFF_ALLOC:
                current['type11_start'], current['type11_end'] = parse_start_end(line)
            elif typ == TYPE_UMPQ_POST:
                current['type12_start'], current['type12_end'] = parse_start_end(line)
            elif typ == TYPE_BUFFER_ENQUEUE:
                ts, _ = parse_start_end(line)
                if current['type14_first'] is None:
                    current['type14_first'] = ts
                current['type14_last'] = ts
            elif typ == TYPE_EPOLL_END:
                current['type13_start'], _ = parse_start_end(line)
                rounds.append(current)
                current = None
    if current:
        rounds.append(current)
    return rounds

def extract_read_rounds(lines):
    rounds = []
    current = None
    for line in lines:
        typ = parse_type(line)
        if typ is None:
            continue
        if typ == TYPE_READ_DEQUEUE:
            if current is None:
                current = {
                    'type6_first': None, 'type6_last': None,
                    'type5_start': None, 'type5_end': None,
                    'type7_start': None, 'type7_end': None,
                    'type3_start': None,
                }
            ts, _ = parse_start_end(line)
            if current['type6_first'] is None:
                current['type6_first'] = ts
            current['type6_last'] = ts
        elif current:
            if typ == TYPE_POLL_QBUF:
                current['type5_start'], current['type5_end'] = parse_start_end(line)
            elif typ == TYPE_DATASET:
                current['type7_start'], current['type7_end'] = parse_start_end(line)
            elif typ == TYPE_READV_END:
                current['type3_start'], _ = parse_start_end(line)
                rounds.append(current)
                current = None
    if current:
        rounds.append(current)
    return rounds

# ========== 构建输出行 ==========
def build_rows_for_side(side, write_data, epoll_rounds, read_rounds):
    """
    根据 side 名称 ('Client' 或 'Server') 构建输出行列表。
    返回 rows 列表，每个元素为 (编号, 描述, type, count, timestamp, cost time, 备注)
    """
    rows = []
    is_client = (side == 'Client')

    # 基础编号
    if is_client:
        write_base = CLIENT_WRITE_BASE
        epoll_base = CLIENT_EPOLL_BASE
        epoll_rows_per_round = EPOLL_ROWS_PER_ROUND_CLIENT
    else:
        write_base = SERVER_WRITE_BASE
        epoll_base = SERVER_EPOLL_BASE
        epoll_rows_per_round = EPOLL_ROWS_PER_ROUND_SERVER

    # ---- Write 阶段 ----
    if is_client:
        rows.append((str(write_base), 'bRPC start', str(TYPE_WRITE_START), '',
                      str(write_data.get('brpc_start', '')), '', ''))
        rows.append(('', 'bRPC end', '暂未统计', '', '', '', ''))
        rows.append((str(write_base+1), 'writeV入口', str(TYPE_WRITEV), '',
                      str(write_data.get('writev_start', '')), '', ''))
        rows.append((str(write_base+2), 'post前', str(TYPE_POST), '',
                      str(write_data.get('post_before', '')), '', ''))
        rows.append((str(write_base+3), 'post后', str(TYPE_POST), '',
                      str(write_data.get('post_after', '')), '', ''))
    else:
        rows.append((str(write_base), 'writeV入口', str(TYPE_WRITEV), '',
                      str(write_data.get('writev_start', '')), '', ''))
        rows.append((str(write_base+1), 'post前', str(TYPE_POST), '',
                      str(write_data.get('post_before', '')), '', ''))
        rows.append((str(write_base+2), 'post后', str(TYPE_POST), '',
                      str(write_data.get('post_after', '')), '', ''))

    # ---- Epoll 轮次 ----
    for idx, rnd in enumerate(epoll_rounds):
        base = epoll_base + idx * epoll_rows_per_round
        rows.append((str(base), 'UmqRearm前', str(TYPE_EPOLL_REARM),
                      str(rnd.get('offset9', '')), str(rnd.get('type9_start', '')), '',
                      '使用offset记录，对应本次poll的buffer数量'))
        rows.append((str(base+1), 'UmqRearm后', str(TYPE_EPOLL_REARM), '',
                      str(rnd.get('type9_end', '')), '', ''))
        rows.append((str(base+2), 'recv thread poll前', str(TYPE_RECV_POLL), '',
                      str(rnd.get('type10_start', '')), '', ''))
        rows.append((str(base+3), 'recv thread poll后', str(TYPE_RECV_POLL), '',
                      str(rnd.get('type10_end', '')), '', ''))
        rows.append((str(base+4), 'umq_buff_alloc前', str(TYPE_BUFF_ALLOC), '',
                      str(rnd.get('type11_start', '')), '', ''))
        rows.append((str(base+5), 'umq_buff_alloc后', str(TYPE_BUFF_ALLOC), '',
                      str(rnd.get('type11_end', '')), '', ''))
        rows.append((str(base+6), 'umq_post前', str(TYPE_UMPQ_POST), '',
                      str(rnd.get('type12_start', '')), '', ''))
        rows.append((str(base+7), 'umq_post后', str(TYPE_UMPQ_POST), '',
                      str(rnd.get('type12_end', '')), '', ''))
        rows.append((str(base+8), 'buffer入队列第一个', str(TYPE_BUFFER_ENQUEUE), '',
                      str(rnd.get('type14_first', '')), '',
                      '此处应有X条打印，只取第1条'))
        rows.append((str(base+9), 'buffer入队列最后一个', str(TYPE_BUFFER_ENQUEUE), '',
                      str(rnd.get('type14_last', '')), '',
                      '此处应有X条打印，只取第X条'))
        desc = '触发事件后process jfr end' if idx == 0 else 'process jfr end'
        rows.append((str(base+10), desc, str(TYPE_EPOLL_END), '',
                      str(rnd.get('type13_start', '')), '', ''))

    # ---- Read 轮次 ----
    read_base = epoll_base + len(epoll_rounds) * epoll_rows_per_round
    for idx, rnd in enumerate(read_rounds):
        base = read_base + idx * READ_ROWS_PER_ROUND
        rows.append((str(base), 'read出队第一个', str(TYPE_READ_DEQUEUE), '',
                      str(rnd.get('type6_first', '')), '', 'event事件时间'))
        rows.append((str(base+1), 'read出队最后一个', str(TYPE_READ_DEQUEUE), '',
                      str(rnd.get('type6_last', '')), '', ''))
        rows.append((str(base+2), 'poll Qbuf前', str(TYPE_POLL_QBUF), '',
                      str(rnd.get('type5_start', '')), '', ''))
        rows.append((str(base+3), 'poll Qbuf后', str(TYPE_POLL_QBUF), '',
                      str(rnd.get('type5_end', '')), '', ''))
        rows.append((str(base+4), 'dataset之前', str(TYPE_DATASET), '',
                      str(rnd.get('type7_start', '')), '', ''))
        rows.append((str(base+5), 'dataset之后', str(TYPE_DATASET), '',
                      str(rnd.get('type7_end', '')), '', ''))
        rows.append((str(base+6), 'readv结束', str(TYPE_READV_END), '',
                      str(rnd.get('type3_start', '')), '', ''))

    return rows

# ========== 输出函数 ==========
def print_table(title, rows):
    fmt = "{:<6}{:<25}{:<6}{:<8}{:<25}{:<12}{:<30}"
    print(f"\n========== {title} ==========")
    print(fmt.format('编号', '描述', 'type', 'count', 'timestamp', 'cost time', '备注'))
    print('-' * 112)
    for r in rows:
        print(fmt.format(*r))

def write_csv(prefix, client_rows, server_rows):
    csv_header = ['编号', '描述', 'type', 'count', 'timestamp', 'cost time', '备注']
    for suffix, rows in [('_client.csv', client_rows), ('_server.csv', server_rows)]:
        with open(f'{prefix}{suffix}', 'w', newline='', encoding='utf-8-sig') as f:
            writer = csv.writer(f, quoting=csv.QUOTE_ALL)
            writer.writerow(csv_header)
            for row in rows:
                writer.writerow([str(item) for item in row])
        print(f"[CSV] 已导出：{prefix}{suffix}")

# ========== 主函数 ==========
def main():
    parser = argparse.ArgumentParser(description='UBSOCKET 性能分析工具')
    parser.add_argument('--client', required=True, help='Client 日志文件路径')
    parser.add_argument('--server', required=True, help='Server 日志文件路径')
    parser.add_argument('--seq', type=int, default=81, help='消息头的 seq 值（必须为 type=26 或 type=2）')
    parser.add_argument('--data-size', type=int, default=102400, help='数据部分总大小（字节）')
    parser.add_argument('--payload-size', type=int, default=4064, help='每片负载大小（字节）')
    parser.add_argument('--dump', action='store_true', help='打印过滤后的行内容，用于对齐验证')
    parser.add_argument('--output-csv', '-o', metavar='PREFIX', help='输出 CSV 文件前缀')
    args = parser.parse_args()

    packet_cnt = 1 + math.ceil(args.data_size / args.payload_size)
    start_seq = args.seq
    end_seq = start_seq + packet_cnt - 1

    with open(args.client) as f:
        all_clines = f.readlines()
    with open(args.server) as f:
        all_slines = f.readlines()

    # 按类别分离
    def split_by_category(lines):
        cat_dict = {'Write': [], 'Epoll': [], 'Read': []}
        for line in lines:
            cat = parse_category(line)
            if cat in cat_dict:
                cat_dict[cat].append(line)
        return cat_dict

    c_cats = split_by_category(all_clines)
    s_cats = split_by_category(all_slines)

    # 校验消息头
    if not validate_header_seq(c_cats['Write'], start_seq, 'Client') or \
       not validate_header_seq(s_cats['Write'], start_seq, 'Server'):
        sys.exit(1)

    # 过滤
    c_write = filter_lines_by_seq(c_cats['Write'], start_seq, end_seq, include_zero=False)
    s_write = filter_lines_by_seq(s_cats['Write'], start_seq, end_seq, include_zero=False)
    c_epoll = filter_epoll_by_range(c_cats['Epoll'], start_seq, end_seq)
    s_epoll = filter_epoll_by_range(s_cats['Epoll'], start_seq, end_seq)
    c_read  = filter_lines_by_seq(c_cats['Read'], start_seq, end_seq, include_zero=False)
    s_read  = filter_lines_by_seq(s_cats['Read'], start_seq, end_seq, include_zero=False)

    if args.dump:
        print("\n===== Dump of filtered lines =====")
        for label, w, e, r in [('Client', c_write, c_epoll, c_read),
                                ('Server', s_write, s_epoll, s_read)]:
            for name, lst in [('Write', w), ('Epoll', e), ('Read', r)]:
                print(f"\n--- {label} {name} ({len(lst)} lines) ---")
                for i, line in enumerate(lst, 1):
                    print(f"  [{i}] {line.rstrip()}")
        print("===== End of dump =====\n")

    # 提取数据
    c_write_data = extract_write(c_write)
    s_write_data = extract_write(s_write)
    c_epoll_rounds = extract_epoll_rounds(c_epoll)
    s_epoll_rounds = extract_epoll_rounds(s_epoll)
    c_read_rounds = extract_read_rounds(c_read)
    s_read_rounds = extract_read_rounds(s_read)

    print(f"[INFO] Client: Write={len(c_write)}, Epoll={len(c_epoll)}, Read={len(c_read)}")
    print(f"[INFO] Server: Write={len(s_write)}, Epoll={len(s_epoll)}, Read={len(s_read)}")

    # 构建行
    client_rows = build_rows_for_side('Client', c_write_data, c_epoll_rounds, c_read_rounds)
    server_rows = build_rows_for_side('Server', s_write_data, s_epoll_rounds, s_read_rounds)

    # 输出表格
    print_table('Client 侧', client_rows)
    print_table('Server 侧', server_rows)

    # 输出 CSV
    if args.output_csv:
        write_csv(args.output_csv, client_rows, server_rows)

if __name__ == "__main__":
    main()