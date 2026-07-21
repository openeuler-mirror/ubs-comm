#!/usr/bin/env python3
"""
UBSOCKET 性能分析工具 ? 最终版（Read 过滤基于 type3 结尾）
"""

import re, sys, math, argparse, csv, bisect

# ========== 常量定义 ==========
EPOLL_ROWS_PER_ROUND_CLIENT = 12
EPOLL_ROWS_PER_ROUND_SERVER = 11
READ_ROWS_PER_ROUND = 7
CLIENT_WRITE_BASE = 1
SERVER_WRITE_BASE = 1

TYPE_WRITEV = 2
TYPE_POST = 19
TYPE_DATA_SPLIT = 18
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
TYPE_UMQ_ALLOC = 35
TYPE_UMQ_FREE = 37

# ========== 解析函数 ==========
def parse_start_end(line):
    s = re.search(r'start_timestamp:\s*(\d+)', line)
    e = re.search(r'end_timestamp:\s*(\d+)', line)
    return (int(s.group(1)) if s else None,
            int(e.group(1)) if e else None)

def parse_duration(line):
    m = re.search(r'duration:\s*(\d+)', line)
    return int(m.group(1)) if m else None

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

def parse_data_size(line):
    m = re.search(r'data_size:\s*(\d+)', line)
    return int(m.group(1)) if m else None

def parse_raw_socket(line):
    m = re.search(r'raw_socket:\s*(\d+)', line)
    return int(m.group(1)) if m else None

def has_is_first(line):
    return bool(re.search(r'\bis_first:\s*1\b', line))

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
    """
    向前找 type=9 是为了捕获该消息之前已经开始但尚未完成的 epoll 轮次
    向后找 type=13 是为了包含处理该消息后的 epoll 结束标记
    """
    if not epoll_lines:
        return []
    pos_start = next((i for i, line in enumerate(epoll_lines)
                      if parse_seq(line) == start_seq), None)
    if pos_start is None:
        return []
    begin = pos_start
    for i in range(pos_start, -1, -1):
        if parse_type(epoll_lines[i]) == TYPE_EPOLL_REARM:
            begin = i
            break
    end14 = pos_start
    for i in range(pos_start, len(epoll_lines)):
        if parse_seq(epoll_lines[i]) == end_seq and \
           parse_type(epoll_lines[i]) == TYPE_BUFFER_ENQUEUE:
            end14 = i
    end = end14
    for i in range(end14 + 1, len(epoll_lines)):
        if parse_type(epoll_lines[i]) == TYPE_EPOLL_END:
            end = i
            break
    return epoll_lines[begin:end+1]

def filter_read_by_range(read_lines, start_seq, end_seq):
    """
    从 start_seq 所在行开始，到从 end_seq 所在行向后找到的第一个 type=3 的行结束。
    如果 end_seq 所在行本身就是 type=3，则直接使用该行。
    """
    # 找到 start_seq 所在行索引
    start_idx = None
    for i, line in enumerate(read_lines):
        if parse_seq(line) == start_seq:
            start_idx = i
            break
    if start_idx is None:
        print(f"[WARN] 在 Read 行中找不到起始 seq={start_seq}，返回空")
        return []

    # 找到 end_seq 所在行索引
    end_idx = None
    for i, line in enumerate(read_lines):
        if parse_seq(line) == end_seq:
            end_idx = i
            break
    if end_idx is None:
        print(f"[WARN] 在 Read 行中找不到结束 seq={end_seq}，返回空")
        return []

    # 从 end_idx 开始向后找第一个 type=3
    final_end_idx = None
    for i in range(end_idx, len(read_lines)):
        if parse_type(read_lines[i]) == TYPE_READV_END:
            final_end_idx = i
            break
    if final_end_idx is None:
        print(f"[WARN] 在 seq={end_seq} 之后找不到 type=3，使用 end_seq 所在行作为结尾")
        final_end_idx = end_idx

    return read_lines[start_idx:final_end_idx+1]

# ========== 辅助函数：查找最近的 is_first:1 seq（按大小排序，去重）==========
def find_nearest_is_first_seqs(write_lines, target_seq, limit=50):
    first_seqs_set = set()
    for line in write_lines:
        if has_is_first(line):
            seq = parse_seq(line)
            if seq is not None:
                first_seqs_set.add(seq)
    if not first_seqs_set:
        return []
    first_seqs = sorted(first_seqs_set)
    pos = bisect.bisect_left(first_seqs, target_seq)
    left = max(0, pos - limit)
    right = min(len(first_seqs), pos + limit)
    return first_seqs[left:right]

# ========== Write 范围查找（最终修正版）==========
def find_write_range(write_lines, target_seq, total_data_size, side):
    """
     is_first:1 标记消息头部的 packet
     向前找 type=2 确定消息的真正起始
     以 seq - target_seq 作偏移累计 data_size，遇到 seq 回退（重试）时回退到该偏移已有累计值继续
     向后找 type=19 作为可靠的结束标记
    """
    target_idx = None
    target_line = None
    for i, line in enumerate(write_lines):
        if parse_seq(line) == target_seq and has_is_first(line):
            target_idx = i
            target_line = line
            break
    if target_idx is None:
        nearest = find_nearest_is_first_seqs(write_lines, target_seq, limit=50)
        msg = f"[ERROR] [{side}] 在 Write 行中找不到 seq={target_seq} 且 is_first: 1 的行。"
        if nearest:
            msg += f" 附近包含 is_first: 1 的 seq 有：{nearest[:100]}"
        else:
            msg += " 整个日志中没有任何包含 is_first: 1 的行。"
        print(msg)
        return None

    # 向前找到第一个 type=2 的行作为起始
    start_idx = None
    start_seq = None
    for i in range(target_idx, -1, -1):
        if parse_type(write_lines[i]) == TYPE_WRITEV:
            start_idx = i
            start_seq = parse_seq(write_lines[i])
            break
    if start_idx is None:
        print(f"[WARN] [{side}] 在 seq={target_seq} 之前找不到 type=2 的行，使用当前 is_first:1 的行作为起始")
        start_idx = target_idx
        start_seq = target_seq

    # 用 seq - target_seq 作偏移索引，记录每个偏移位置处理前的累计值
    # 回退时恢复到该偏移已有的累计值继续，不清零，保留回退点之前的数据
    offset_to_accum = {}
    accumulated = 0
    candidate_idx = None
    candidate_seq = None
    last_seq = None

    for i in range(target_idx, len(write_lines)):
        line = write_lines[i]
        typ = parse_type(line)
        seq = parse_seq(line)
        ds = parse_data_size(line)
        if typ in (TYPE_DATA_SPLIT, TYPE_POST, TYPE_WRITEV) and ds is not None and seq is not None:
            offset = seq - target_seq
            if offset < 0:
                continue

            if last_seq is not None and seq < last_seq:
                # seq 回退：回到该偏移处已有的累计值，清除 offset 及之后的旧记录
                accumulated = offset_to_accum.get(offset, 0)
                for k in sorted(offset_to_accum.keys()):
                    if k >= offset:
                        del offset_to_accum[k]
                candidate_idx = None
                candidate_seq = None

            if last_seq is not None and seq == last_seq:
                continue

            offset_to_accum[offset] = accumulated
            accumulated += ds
            last_seq = seq

            if accumulated >= total_data_size and candidate_idx is None:
                candidate_idx = i
                candidate_seq = seq
                break
    if candidate_idx is None:
        print(f"[ERROR] [{side}] 从 seq={target_seq} 开始向后找不到足够的数据，累计 data_size={accumulated}，目标={total_data_size}")
        return None

    packet_cnt = candidate_seq - start_seq + 1

    end_idx = None
    end_seq = None
    for i in range(candidate_idx, len(write_lines)):
        typ = parse_type(write_lines[i])
        seq = parse_seq(write_lines[i])
        if typ == TYPE_POST:
            if seq is not None and seq >= candidate_seq:
                end_idx = i
                end_seq = seq
                break
    if end_idx is None:
        print(f"[ERROR] [{side}] 从 candidate_idx 起找不到 type=19，请检查日志完整性")
        return None

    if end_seq != candidate_seq:
        print(f"[WARN] [{side}] 候选结尾 seq={candidate_seq} (data_size累加到达点) 与 type=19 结尾 seq={end_seq} 不一致")

    filtered = write_lines[start_idx:end_idx+1]
    print(f"[INFO] [{side}] 消息范围：seq {start_seq} ~ {end_seq}，packet_cnt={packet_cnt}，累计 data_size={accumulated}（目标={total_data_size}）")
    return (start_seq, end_seq, filtered)

# ========== 辅助函数 ==========
def format_duration(duration):
    return str(duration) if duration is not None and duration != 0 else ""

def safe_tuple(t, index):
    if isinstance(t, tuple) and len(t) > index:
        val = t[index]
        return str(val) if val is not None else ""
    return ""

# ========== 类别提取函数（支持多个writev分组）==========
def extract_write(lines):
    result = {'brpc': None, 'writevs': []}
    current_group = None
    for line in lines:
        typ = parse_type(line)
        s, e = parse_start_end(line)
        d = parse_duration(line)
        if typ == TYPE_WRITEV:
            if result['brpc'] is None:
                result['brpc'] = (s, e, d)
            if current_group is not None:
                result['writevs'].append(current_group)
            current_group = {'writev': (s,e,d), 'umq_alloc': None, 'umq_free': None, 'post': None,
                             'start_seq': parse_seq(line), 'end_seq': None}
        elif typ == TYPE_UMQ_ALLOC and current_group is not None:
            current_group['umq_alloc'] = (s,e,d)
        elif typ == TYPE_UMQ_FREE and current_group is not None:
            current_group['umq_free'] = (s,e,d)
        elif typ == TYPE_POST and current_group is not None:
            current_group['post'] = (s,e,d)
            current_group['end_seq'] = parse_seq(line)
            result['writevs'].append(current_group)
            current_group = None
    if current_group is not None:
        result['writevs'].append(current_group)
    return result

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
            s, e = parse_start_end(line)
            d = parse_duration(line)
            current = {
                'offset9': parse_offset(line),
                'type9': (s, e, d),
                'type10': None,
                'type11': None,
                'type12': None,
                'type14_first': None,
                'type14_first_seq': None,
                'type14_last': None,
                'type14_last_seq': None,
                'type13': None,
                'type13_seq': None,
            }
        elif current:
            s, e = parse_start_end(line)
            d = parse_duration(line)
            if typ == TYPE_RECV_POLL:
                current['type10'] = (s, e, d)
            elif typ == TYPE_BUFF_ALLOC:
                current['type11'] = (s, e, d)
            elif typ == TYPE_UMPQ_POST:
                current['type12'] = (s, e, d)
            elif typ == TYPE_BUFFER_ENQUEUE:
                ts, _ = s, e
                seq = parse_seq(line)
                if current['type14_first'] is None:
                    current['type14_first'] = ts
                    current['type14_first_seq'] = seq
                current['type14_last'] = ts
                current['type14_last_seq'] = seq
            elif typ == TYPE_EPOLL_END:
                current['type13'] = (s, e, d)
                current['type13_seq'] = parse_seq(line)
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
            seq = parse_seq(line)
            if current is None:
                current = {
                    'type6_first': None,
                    'type6_first_seq': None,
                    'type6_last': None,
                    'type6_last_seq': None,
                    'type5': None,
                    'type7': None,
                    'type3': None,
                }
            ts, _ = parse_start_end(line)
            if current['type6_first'] is None:
                current['type6_first'] = ts
                current['type6_first_seq'] = seq
            current['type6_last'] = ts
            current['type6_last_seq'] = seq
        elif current:
            s, e = parse_start_end(line)
            d = parse_duration(line)
            if typ == TYPE_POLL_QBUF:
                current['type5'] = (s, e, d)
            elif typ == TYPE_DATASET:
                current['type7'] = (s, e, d)
            elif typ == TYPE_READV_END:
                current['type3'] = (s, e, d)
                rounds.append(current)
                current = None
    if current:
        rounds.append(current)
    return rounds

# ========== UMQ 日志解析 ==========
def parse_umq_entries(lines):
    """解析 UMQ trace 日志为结构化条目列表（同时保留原始行）

    新格式：header 行末尾带 ';' 后接 items，多个 item/sub 用 ';' 串在同一行。
    """
    entries = []
    current = None
    current_raw = []

    header_re = re.compile(r'#(\d+)\s+type=(\w+)\s+umq_id=(\d+)\s+umq_start=(\d+)\s+umq_end=(\d+)\s+umq_exec=(\d+)\s+item_cnt=(\d+)\s+ts=(\d+)\s+tag_ts=(\d+)')
    item_re = re.compile(r'item\[(\d+)\]\s+umq_id=(\d+)\s+(?:sub_umq_id=(\d+)\s+)?msn=(\d+)\s+size=(\d+)')
    sub_re = re.compile(r'sub\[(\d+)\]\s+umq_id=(\d+)\s+func=(.+?)\s+start=(\d+)\s+exec=(\d+)')

    for line in lines:
        m = header_re.search(line)
        if m:
            if current:
                current['_raw'] = current_raw
                entries.append(current)
            current = {
                'num': int(m.group(1)),
                'type': m.group(2),
                'umq_id': int(m.group(3)),
                'umq_start': int(m.group(4)),
                'umq_end': int(m.group(5)),
                'umq_exec': int(m.group(6)),
                'item_cnt': int(m.group(7)),
                'ts': int(m.group(8)),
                'interrupt_ts': int(m.group(9)),
                'items': [],
                'subs': [],
            }
            current_raw = [line]
        elif current is not None:
            current_raw.append(line)

        if current is None:
            continue

        # 按 ';' 拆段，逐段匹配 item/sub
        for seg in [s.strip() for s in line.split(';') if s.strip()]:
            if header_re.search(seg):
                continue

            mi = item_re.search(seg)
            if mi:
                item = {
                    'idx': int(mi.group(1)),
                    'umq_id': int(mi.group(2)),
                    'msn': int(mi.group(4)),
                    'size': int(mi.group(5)),
                }
                sub_umq = mi.group(3)
                if sub_umq is not None:
                    item['sub_umq_id'] = int(sub_umq)
                current['items'].append(item)
                continue

            ms = sub_re.search(seg)
            if ms:
                current['subs'].append({
                    'idx': int(ms.group(1)),
                    'umq_id': int(ms.group(2)),
                    'func': ms.group(3),
                    'start': int(ms.group(4)),
                    'exec': int(ms.group(5)),
                })

    if current:
        current['_raw'] = current_raw
        entries.append(current)

    return entries


def find_umq_batch(umq_entries, start_seq, end_seq, consumed):
    """根据 msn 范围从 UMQ 列表中找第一个未消费的匹配 POST。
       找到后把该 POST 的索引标记为已消费，避免后续重复匹配。"""
    if not umq_entries:
        return None

    for i, entry in enumerate(umq_entries):
        if i in consumed:
            continue
        if entry['type'] == 'POST' and entry.get('items'):
            first_msn = entry['items'][0]['msn']
            last_msn = entry['items'][-1]['msn']
            if first_msn == start_seq and last_msn == end_seq:
                consumed.add(i)
                return [entry]

    return None


def find_umq_epoll_batch(umq_entries, type9_start_ts):
    """根据 type9 的 start_timestamp 匹配 UMQ 批次：
       interrupt_ts 匹配的连续条目 + 紧随其后的第一个 POLL"""
    if not umq_entries or type9_start_ts is None:
        return None

    matched_indices = []
    for i, entry in enumerate(umq_entries):
        if entry.get('interrupt_ts') == type9_start_ts:
            matched_indices.append(i)

    if not matched_indices:
        return None

    batch_start = matched_indices[0]
    batch_end = matched_indices[-1] + 1  # 含最后一个匹配条目

    # 从最后一个匹配条目往后找第一个 POLL（包含）
    for i in range(matched_indices[-1] + 1, len(umq_entries)):
        if umq_entries[i]['type'] == 'POLL':
            batch_end = i + 1
            break

    return umq_entries[batch_start:batch_end]


def _build_umq_epoll_sub_rows(umq_entries, rnd, rnd_key='type9', label=''):
    """为单个 epoll 轮次生成 UMQ/URMA 关联行（用 rnd[rnd_key].start_ts 匹配 UMQ 条目中的 tag_ts）"""
    rows = []
    data = rnd.get(rnd_key)
    if data is None:
        return rows
    ts = data[0]
    if ts is None:
        return rows

    batch = find_umq_epoll_batch(umq_entries, ts)
    if batch is None:
        return rows

    for entry in batch:
        if rnd_key == 'type12' and entry['type'] != 'POST':
            continue
        remark = '#{}'.format(entry['num'])
        if label:
            remark += ' ({}) tag_ts={}'.format(label, entry.get('interrupt_ts', ''))
        rows.append(('', 'UMQ#{} {}'.format(entry['num'], entry['type']),
                     entry['type'], str(entry['item_cnt']),
                     str(entry.get('umq_start', '')), str(entry.get('umq_end', '')),
                     str(entry.get('umq_exec', '')), remark))
        for sub in entry.get('subs', []):
            sub_end = '' if sub['start'] is None or sub['exec'] is None \
                else str(sub['start'] + sub['exec'])
            rows.append(('', '  ' + sub['func'], 'URMA', '',
                         str(sub['start']), sub_end, str(sub['exec']), ''))
    return rows


def _find_first_urma_wait_rx_end(epoll_rounds, umq_entries):
    """从 type9 关联的 UMQ 批次中找到第一个 urma_wait_jfc(rx) 的结束时间(start+exec)"""
    if not umq_entries or not epoll_rounds:
        return None
    for rnd in epoll_rounds:
        t9 = rnd.get('type9')
        if t9 is None or t9[0] is None:
            continue
        batch = find_umq_epoll_batch(umq_entries, t9[0])
        if batch is None:
            continue
        for entry in batch:
            for sub in entry.get('subs', []):
                if sub['func'] == 'urma_wait_jfc(rx)' and sub['start'] is not None and sub['exec'] is not None:
                    return sub['start'] + sub['exec']
    return None


# ========== 构建输出行 ==========
def _build_umq_sub_rows_from_batch(batch):
    """根据已找到的 UMQ batch 生成关联行"""
    rows = []
    if batch is None:
        return rows
    for entry in batch:
        rows.append(('', 'UMQ#{} {}'.format(entry['num'], entry['type']),
                     entry['type'], str(entry['item_cnt']),
                     str(entry.get('umq_start', '')), str(entry.get('umq_end', '')),
                     str(entry.get('umq_exec', '')), '#{}'.format(entry['num'])))
        for sub in entry.get('subs', []):
            sub_end = '' if sub['start'] is None or sub['exec'] is None \
                else str(sub['start'] + sub['exec'])
            rows.append(('', '  ' + sub['func'], 'URMA', '',
                         str(sub['start']), sub_end, str(sub['exec']), ''))
    return rows


def _compute_side_sums(rows):
    """扫描所有行，分别累加 UMQ 和 URMA 的 duration"""
    umq_sum = 0
    urma_sum = 0
    for row in rows:
        typ = row[2]
        dur_str = row[6]
        if not dur_str:
            continue
        dur = int(dur_str)
        if typ in ('POLL', 'POST', 'WAIT', 'REARM'):
            umq_sum += dur
        elif typ == 'URMA':
            urma_sum += dur
    return umq_sum, urma_sum

def build_rows_for_side(side, write_data, epoll_rounds, read_rounds, umq_entries=None):
    rows = []
    is_client = (side == 'Client')
    consumed = set()

    if is_client:
        rows.append((str(CLIENT_WRITE_BASE), 'bRPC start', '暂未统计', '',
                      '', '', '', ''))
        rows.append(('', 'bRPC end', '暂未统计', '', '', '', '', ''))
        base = CLIENT_WRITE_BASE + 1
        writevs = write_data.get('writevs', [])
        for idx, group in enumerate(writevs):
            num = base + idx * 4
            writev = group.get('writev')
            seq_info = f"seq: {group.get('start_seq', '?')}~{group.get('end_seq', '?')}"
            rows.append((str(num), 'writeV入口', str(TYPE_WRITEV), '',
                          safe_tuple(writev, 0), safe_tuple(writev, 1),
                          format_duration(writev[2] if writev else None), seq_info))
            alloc = group.get('umq_alloc')
            rows.append((str(num+1), 'umq_alloc', str(TYPE_UMQ_ALLOC), '',
                          safe_tuple(alloc, 0), safe_tuple(alloc, 1),
                          format_duration(alloc[2] if alloc else None), ''))
            free = group.get('umq_free')
            rows.append((str(num+2), 'umq_free', str(TYPE_UMQ_FREE), '',
                          safe_tuple(free, 0), safe_tuple(free, 1),
                          format_duration(free[2] if free else None), ''))
            post = group.get('post')
            rows.append((str(num+3), 'post', str(TYPE_POST), '',
                          safe_tuple(post, 0), safe_tuple(post, 1),
                          format_duration(post[2] if post else None), ''))
            if umq_entries:
                batch = find_umq_batch(umq_entries, group.get('start_seq'),
                                        group.get('end_seq'), consumed)
                rows.extend(_build_umq_sub_rows_from_batch(batch))
        epoll_base = CLIENT_WRITE_BASE + 1 + 4 * len(writevs)
        epoll_rows_per_round = EPOLL_ROWS_PER_ROUND_CLIENT
    else:
        base = SERVER_WRITE_BASE
        writevs = write_data.get('writevs', [])
        for idx, group in enumerate(writevs):
            num = base + idx * 4
            writev = group.get('writev')
            seq_info = f"seq: {group.get('start_seq', '?')}~{group.get('end_seq', '?')}"
            rows.append((str(num), 'writeV入口', str(TYPE_WRITEV), '',
                          safe_tuple(writev, 0), safe_tuple(writev, 1),
                          format_duration(writev[2] if writev else None), seq_info))
            alloc = group.get('umq_alloc')
            rows.append((str(num+1), 'umq_alloc', str(TYPE_UMQ_ALLOC), '',
                          safe_tuple(alloc, 0), safe_tuple(alloc, 1),
                          format_duration(alloc[2] if alloc else None), ''))
            free = group.get('umq_free')
            rows.append((str(num+2), 'umq_free', str(TYPE_UMQ_FREE), '',
                          safe_tuple(free, 0), safe_tuple(free, 1),
                          format_duration(free[2] if free else None), ''))
            post = group.get('post')
            rows.append((str(num+3), 'post', str(TYPE_POST), '',
                          safe_tuple(post, 0), safe_tuple(post, 1),
                          format_duration(post[2] if post else None), ''))
            if umq_entries:
                batch = find_umq_batch(umq_entries, group.get('start_seq'),
                                        group.get('end_seq'), consumed)
                rows.extend(_build_umq_sub_rows_from_batch(batch))
        epoll_base = SERVER_WRITE_BASE + 4 * len(writevs)
        epoll_rows_per_round = EPOLL_ROWS_PER_ROUND_SERVER

    for idx, rnd in enumerate(epoll_rounds):
        base = epoll_base + idx * epoll_rows_per_round

        t9_data = rnd.get('type9')
        t13_data = rnd.get('type13')
        if t9_data and isinstance(t9_data, tuple) and t9_data[0] is not None \
           and t13_data and isinstance(t13_data, tuple) and t13_data[0] is not None:
            t9_start = t9_data[0]
            t13_start = t13_data[0]
            dur = int(t13_start) - int(t9_start)
            ep_remark = ''
            fs = rnd.get('type14_first_seq')
            ls = rnd.get('type14_last_seq')
            if fs is not None and ls is not None:
                ep_remark = 'seq:{}~{}'.format(fs, ls)
            rows.append(('', 'process one event', '9/13', '',
                         str(t9_start), str(t13_start),
                         format_duration(dur), ep_remark))
        else:
            rows.append(('', 'process one event', '9/13', '', '', '', '', ''))

        if umq_entries:
            rows.extend(_build_umq_epoll_sub_rows(umq_entries, rnd))
            rows.extend(_build_umq_epoll_sub_rows(umq_entries, rnd, 'type12', 'type12关联'))

        # async epoll event: read出队第一个 - buffer入队最后一个
        first_seq = rnd.get('type14_first_seq')
        last_seq = rnd.get('type14_last_seq')
        if first_seq is not None and last_seq is not None:
            t14_last = rnd.get('type14_last')
            if t14_last is not None:
                for rd in read_rounds:
                    rd_first = rd.get('type6_first_seq')
                    rd_last = rd.get('type6_last_seq')
                    t6_first = rd.get('type6_first')
                    if rd_first is not None and rd_last is not None and t6_first is not None and \
                       rd_first <= last_seq and rd_last >= first_seq:
                        async_dur = int(t6_first) - int(t14_last)
                        remark = 'type6_first={} - type14_last={} (readv seq:{}~{})'.format(
                            t6_first, t14_last, rd_first, rd_last)
                        rows.append(('', 'async epoll event', '', '',
                                     str(t14_last), str(t6_first),
                                     format_duration(async_dur), remark))
                        break

        t9 = rnd.get('type9')
        rows.append((str(base), 'UmqRearm', str(TYPE_EPOLL_REARM),
                      str(rnd.get('offset9', '')),
                      safe_tuple(t9, 0), safe_tuple(t9, 1),
                      format_duration(t9[2] if t9 else None),
                      '使用offset记录，对应本次poll的buffer数量'))
        t10 = rnd.get('type10')
        rows.append((str(base+1), 'recv thread poll', str(TYPE_RECV_POLL), '',
                      safe_tuple(t10, 0), safe_tuple(t10, 1),
                      format_duration(t10[2] if t10 else None), ''))
        t11 = rnd.get('type11')
        rows.append((str(base+2), 'umq_buff_alloc', str(TYPE_BUFF_ALLOC), '',
                      safe_tuple(t11, 0), safe_tuple(t11, 1),
                      format_duration(t11[2] if t11 else None), ''))
        t12 = rnd.get('type12')
        rows.append((str(base+3), 'umq_post', str(TYPE_UMPQ_POST), '',
                      safe_tuple(t12, 0), safe_tuple(t12, 1),
                      format_duration(t12[2] if t12 else None), ''))
        rows.append((str(base+4), 'buffer入队列第一个', str(TYPE_BUFFER_ENQUEUE), '',
                      str(rnd.get('type14_first', '')), '', '', '此处应有X条打印，只取第1条'))
        rows.append((str(base+5), 'buffer入队列最后一个', str(TYPE_BUFFER_ENQUEUE), '',
                      str(rnd.get('type14_last', '')), '', '', '此处应有X条打印，只取第X条'))
        t13 = rnd.get('type13')
        desc = '触发事件后process jfr end' if idx == 0 else 'process jfr end'
        rows.append((str(base+6), desc, str(TYPE_EPOLL_END), '',
                      safe_tuple(t13, 0), safe_tuple(t13, 1),
                      format_duration(t13[2] if t13 else None), ''))

    read_base = epoll_base + len(epoll_rounds) * epoll_rows_per_round
    for idx, rnd in enumerate(read_rounds):
        base = read_base + idx * READ_ROWS_PER_ROUND
        rows.append((str(base), 'read出队第一个', str(TYPE_READ_DEQUEUE), '',
                      str(rnd.get('type6_first', '')), '', '', 'event事件时间'))
        rows.append((str(base+1), 'read出队最后一个', str(TYPE_READ_DEQUEUE), '',
                      str(rnd.get('type6_last', '')), '', '', ''))
        t5 = rnd.get('type5')
        rows.append((str(base+2), 'poll Qbuf', str(TYPE_POLL_QBUF), '',
                      safe_tuple(t5, 0), safe_tuple(t5, 1),
                      format_duration(t5[2] if t5 else None), ''))
        t7 = rnd.get('type7')
        rows.append((str(base+3), 'dataset', str(TYPE_DATASET), '',
                      safe_tuple(t7, 0), safe_tuple(t7, 1),
                      format_duration(t7[2] if t7 else None), ''))
        t3 = rnd.get('type3')
        r3_remark = ''
        fs = rnd.get('type6_first_seq')
        ls = rnd.get('type6_last_seq')
        if fs is not None and ls is not None:
            r3_remark = 'seq:{}~{}'.format(fs, ls)
        rows.append((str(base+4), 'readv结束', str(TYPE_READV_END), '',
                      safe_tuple(t3, 0), safe_tuple(t3, 1),
                       format_duration(t3[2] if t3 else None), r3_remark))

    umq_sum, urma_sum = _compute_side_sums(rows)
    rows.append(('', '', '', '', '', '', '', ''))
    rows.append(('', 'UMQ SUM', '', '', '', '', format_duration(umq_sum), ''))
    rows.append(('', 'urma SUM', '', '', '', '', format_duration(urma_sum), ''))

    write_sum = 0
    for group in write_data.get('writevs', []):
        wv = group.get('writev')
        if wv and wv[2]:
            write_sum += wv[2]
    rows.append(('', 'write SUM', '', '', '', '',
                 format_duration(write_sum), '所有writeV入口(type=2)的duration总和'))

    epoll_sum = 0
    for row in rows:
        if row[1] == 'process one event' and row[6]:
            epoll_sum += int(row[6])
    rows.append(('', 'epoll SUM', '', '', '', '',
                 format_duration(epoll_sum), 'sum(process one event)'))

    read_sum = 0
    for rnd in read_rounds:
        t3 = rnd.get('type3')
        if t3 and t3[2]:
            read_sum += t3[2]
    rows.append(('', 'read SUM', '', '', '', '',
                 format_duration(read_sum), '所有readv结束(type=3)的duration总和'))

    ubsocket_sum = write_sum + epoll_sum + read_sum
    rows.append(('', 'ubsocket SUM', '', '', '', '',
                  format_duration(ubsocket_sum), 'write + epoll + read'))

    side_total = 0
    if is_client:
        first_t14 = epoll_rounds[0].get('type14_first') if epoll_rounds else None
        urma_rx_end = _find_first_urma_wait_rx_end(epoll_rounds, umq_entries)
        actual_start = urma_rx_end if urma_rx_end is not None else (int(first_t14) if first_t14 is not None else None)
        first_t19 = None
        for group in write_data.get('writevs', []):
            p = group.get('post')
            if p and p[1] is not None:
                first_t19 = p[1]
                break
        if actual_start is not None and first_t19 is not None:
            side_total = int(actual_start) - int(first_t19)
            key_name = 'urma_wait_jfc(rx)_end' if urma_rx_end else 'type14_start'
            rows.append(('', 'Client等待耗时', '', '', '', '',
                          format_duration(side_total),
                          f'{key_name}={actual_start} - type19_end={first_t19}'))
    else:
        first_t19 = None
        for group in write_data.get('writevs', []):
            p = group.get('post')
            if p and p[1] is not None:
                first_t19 = p[1]
                break
        first_t14 = epoll_rounds[0].get('type14_first') if epoll_rounds else None
        urma_rx_end = _find_first_urma_wait_rx_end(epoll_rounds, umq_entries)
        actual_start = urma_rx_end if urma_rx_end is not None else (int(first_t14) if first_t14 is not None else None)
        if first_t19 is not None and actual_start is not None:
            side_total = int(first_t19) - int(actual_start)
            key_name = 'urma_wait_jfc(rx)_end' if urma_rx_end else 'type14_start'
            rows.append(('', 'Server处理耗时', '', '', '', '',
                          format_duration(side_total),
                          f'type19_end={first_t19} - {key_name}={actual_start}'))

    return rows, side_total

def _compute_brpc_time(server_rows):
    writev_start = None
    readv_end = None
    for row in server_rows:
        desc = row[1]
        if desc == 'writeV入口' and writev_start is None:
            s = row[4]
            if s:
                writev_start = int(s)
        if desc == 'readv结束':
            e = row[5]
            if e:
                readv_end = int(e)
    if writev_start is None or readv_end is None:
        return
    duration = writev_start - readv_end
    server_rows.append(('', 'BRPC耗时', '', '', '', '',
                         format_duration(duration),
                         f'writeV入口_start={writev_start} - readv结束_end={readv_end}'))

# ========== UMQ/URMA 关联行构建 ==========
def build_umq_urma_rows(side, write_data, umq_entries):
    """对每个 writev 分组，匹配 UMQ 批次并生成关联行"""
    rows = []
    if not umq_entries:
        return rows

    writevs = write_data.get('writevs', [])
    consumed = set()
    for group in writevs:
        start_seq = group.get('start_seq')
        end_seq = group.get('end_seq')
        if start_seq is None or end_seq is None:
            continue

        batch = find_umq_batch(umq_entries, start_seq, end_seq, consumed)
        if batch is None:
            print(f"[WARN] [{side}] 未找到匹配 writev seq {start_seq}~{end_seq} 的 UMQ POST，跳过")
            continue

        rows.append(('', f'-- {side} seq {start_seq}~{end_seq} --', '', '', '', '', '', ''))

        for entry in batch:
            desc = 'UMQ#{} {}'.format(entry['num'], entry['type'])
            umq_start = str(entry.get('umq_start', ''))
            umq_end = str(entry.get('umq_end', ''))
            umq_exec = str(entry.get('umq_exec', ''))
            remark = '#{}'.format(entry['num'])
            rows.append(('', desc, entry['type'], str(entry['item_cnt']), umq_start, umq_end, umq_exec, remark))

            for sub in entry.get('subs', []):
                sub_desc = '  ' + sub['func']
                sub_start = str(sub['start'])
                sub_duration = str(sub['exec'])
                sub_end = '' if sub['start'] is None or sub['exec'] is None else str(sub['start'] + sub['exec'])
                rows.append(('', sub_desc, 'URMA', '', sub_start, sub_end, sub_duration, ''))

    # 生成连续编号
    idx = 1
    numbered = []
    for row in rows:
        numbered.append((str(idx),) + row[1:])
        idx += 1
    return numbered


# ========== 输出函数 ==========
def print_table(title, rows):
    fmt = "{:<6}{:<25}{:<6}{:<8}{:<22}{:<22}{:<12}{:<30}"
    print(f"\n========== {title} ==========")
    print(fmt.format('编号', '描述', 'type', 'count', 'start_timestamp', 'end_timestamp', 'duration', '备注'))
    print('-' * 185)
    for r in rows:
        print(fmt.format(*r))

def write_csv(prefix, client_rows, server_rows, client_only=False):
    csv_header = ['编号', '描述', 'type', 'count', 'start_timestamp', 'end_timestamp', 'duration', '备注']
    if client_only:
        suffixes = [('.csv', client_rows)]
    else:
        suffixes = [('_client.csv', client_rows), ('_server.csv', server_rows)]
    for suffix, rows in suffixes:
        filename = prefix + suffix
        with open(filename, 'w', newline='', encoding='utf-8-sig') as f:
            writer = csv.writer(f, quoting=csv.QUOTE_ALL)
            writer.writerow(csv_header)
            for row in rows:
                writer.writerow([str(item) for item in row])
        print(f"[CSV] 已导出：{filename}")

def find_effective_start(write_data, target_seq):
    """找到 target_seq 最后一次出现的 writev 组（即实际发送的段）。
       返回 (start_seq, start_timestamp)；未找到返回 (None, None)。"""
    for group in reversed(write_data.get('writevs', [])):
        s = group.get('start_seq')
        e = group.get('end_seq')
        if s is not None and e is not None and s <= target_seq <= e:
            wv = group.get('writev')
            ts = wv[0] if wv else None
            return s, ts
    return None, None



# ========== 主函数 ==========
def main():
    parser = argparse.ArgumentParser(description='UBSOCKET 性能分析工具（Read 过滤基于 type3 结尾）')
    parser.add_argument('--client', required=True, help='Client 日志文件路径')
    parser.add_argument('--server', required=True, help='Server 日志文件路径')
    parser.add_argument('--client-seq', type=int, default=None, help='Client 消息头部的 seq 值（该行必须包含 is_first:1）')
    parser.add_argument('--server-seq', type=int, default=None, help='Server 消息头部的 seq 值（该行必须包含 is_first:1）')
    parser.add_argument('--seq', type=int, default=None, help='（可选）同时设置 client-seq 和 server-seq 为相同值')
    parser.add_argument('--data-size', type=int, default=102400, help='数据部分总大小（字节），用于累加 type18/19/26 的 data_size')
    parser.add_argument('--client-fd', type=int, default=None, help='（可选）Client 侧按 raw_socket 值过滤 UBSocket 日志行')
    parser.add_argument('--server-fd', type=int, default=None, help='（可选）Server 侧按 raw_socket 值过滤 UBSocket 日志行')
    parser.add_argument('--dump', action='store_true', help='打印过滤后的行内容')
    parser.add_argument('--output-csv', '-o', metavar='PREFIX', help='输出 CSV 文件前缀')
    args = parser.parse_args()

    if args.seq is not None:
        client_seq = args.seq
        server_seq = args.seq
        if args.client_seq is not None or args.server_seq is not None:
            print("[WARN] 同时指定了 --seq 和 --client-seq/--server-seq，将优先使用 --client-seq/--server-seq")
            client_seq = args.client_seq if args.client_seq is not None else args.seq
            server_seq = args.server_seq if args.server_seq is not None else args.seq
    else:
        client_seq = args.client_seq
        server_seq = args.server_seq

    if client_seq is None or server_seq is None:
        print("[ERROR] 必须指定 --client-seq 和 --server-seq（或使用 --seq 同时指定）")
        sys.exit(1)

    total_data_size = args.data_size

    with open(args.client) as f:
        all_clines = f.readlines()
    with open(args.server) as f:
        all_slines = f.readlines()

    def split_by_category(lines, fd=None):
        cat_dict = {'Write': [], 'Epoll': [], 'Read': [], 'UMQ': []}
        for line in lines:
            cat = parse_category(line)
            if cat in cat_dict:
                if fd is not None:
                    rs = parse_raw_socket(line)
                    if rs is not None and rs == fd:
                        cat_dict[cat].append(line)
                else:
                    cat_dict[cat].append(line)
            elif 'umq_trace_output_single' in line:
                cat_dict['UMQ'].append(line)
        return cat_dict

    c_cats = split_by_category(all_clines, args.client_fd)
    s_cats = split_by_category(all_slines, args.server_fd)

    c_write_result = find_write_range(c_cats['Write'], client_seq, total_data_size, 'Client')
    s_write_result = find_write_range(s_cats['Write'], server_seq, total_data_size, 'Server')
    if c_write_result is None or s_write_result is None:
        sys.exit(1)
    c_write_start_seq, c_write_end_seq, c_write = c_write_result
    s_write_start_seq, s_write_end_seq, s_write = s_write_result

    c_write_data = extract_write(c_write)
    s_write_data = extract_write(s_write)

    # 找到 target_seq 实际发送的 writev（跳过回退失败的段）
    c_eff_seq, c_eff_ts = find_effective_start(c_write_data, client_seq)
    s_eff_seq, s_eff_ts = find_effective_start(s_write_data, server_seq)

    # Epoll 过滤（使用对端有效 write 范围）
    s_eff_start = s_eff_seq if s_eff_seq is not None else s_write_start_seq
    c_eff_start = c_eff_seq if c_eff_seq is not None else c_write_start_seq
    c_epoll = filter_epoll_by_range(c_cats['Epoll'], s_eff_start, s_write_end_seq)
    s_epoll = filter_epoll_by_range(s_cats['Epoll'], c_eff_start, c_write_end_seq)

    # Read 过滤（使用对端有效 write 范围）
    c_read = filter_read_by_range(c_cats['Read'], s_eff_start, s_write_end_seq)
    s_read = filter_read_by_range(s_cats['Read'], c_eff_start, c_write_end_seq)

    # UMQ 解析（提前以支持 --dump 过滤）
    client_umq_entries = parse_umq_entries(c_cats['UMQ'])
    server_umq_entries = parse_umq_entries(s_cats['UMQ'])

    if args.dump:
        print("\n===== Dump of filtered lines =====")
        for label, w, e, r in [('Client', c_write, c_epoll, c_read),
                                ('Server', s_write, s_epoll, s_read)]:
            for name, lst in [('Write', w), ('Epoll', e), ('Read', r)]:
                print(f"\n--- {label} {name} ({len(lst)} lines) ---")
                for i, line in enumerate(lst, 1):
                    print(f"  [{i}] {line.rstrip()}")
        # UMQ: 仅 dump 匹配到的批次
        for label, wd, entries in [('Client', c_write_data, client_umq_entries),
                                    ('Server', s_write_data, server_umq_entries)]:
            consumed = set()
            for group in wd.get('writevs', []):
                start_seq = group.get('start_seq')
                end_seq = group.get('end_seq')
                if start_seq is None or end_seq is None:
                    continue
                batch = find_umq_batch(entries, start_seq, end_seq, consumed)
                if batch:
                    total_lines = sum(len(e['_raw']) for e in batch)
                    print(f"\n--- {label} UMQ matched batch for seq {start_seq}~{end_seq} "
                          f"({len(batch)} entries, {total_lines} lines) ---")
                    for e in batch:
                        print(f"  # --- entry #{e['num']} {e['type']} ---")
                        for raw_line in e['_raw']:
                            print(f"  {raw_line.rstrip()}")
                else:
                    print(f"\n--- {label} UMQ no match for seq {start_seq}~{end_seq} ---")

        # UMQ epoll 关联 dump (type9)
        for label, epoll_lines, entries in [('Client', c_epoll, client_umq_entries),
                                              ('Server', s_epoll, server_umq_entries)]:
            seen_ts = set()
            for line in epoll_lines:
                typ = parse_type(line)
                if typ != TYPE_EPOLL_REARM:
                    continue
                s, _ = parse_start_end(line)
                if s is None or s in seen_ts:
                    continue
                seen_ts.add(s)
                batch = find_umq_epoll_batch(entries, s)
                if batch:
                    total_lines = sum(len(e['_raw']) for e in batch)
                    print(f"\n--- {label} UMQ epoll batch for type9 start_ts={s} "
                          f"({len(batch)} entries, {total_lines} lines) ---")
                    for e in batch:
                        print(f"  # --- entry #{e['num']} {e['type']} ---")
                        for raw_line in e['_raw']:
                            print(f"  {raw_line.rstrip()}")
                else:
                    print(f"\n--- {label} UMQ epoll no match for type9 start_ts={s} ---")

        # UMQ epoll 关联 dump (type12)
        for label, epoll_lines, entries in [('Client', c_epoll, client_umq_entries),
                                              ('Server', s_epoll, server_umq_entries)]:
            seen_ts = set()
            for line in epoll_lines:
                typ = parse_type(line)
                if typ != TYPE_UMPQ_POST:
                    continue
                s, _ = parse_start_end(line)
                if s is None or s in seen_ts:
                    continue
                seen_ts.add(s)
                batch = find_umq_epoll_batch(entries, s)
                if batch:
                    # 仅显示 POST 条目
                    post_entries = [e for e in batch if e['type'] == 'POST']
                    total_lines = sum(len(e['_raw']) for e in post_entries)
                    print(f"\n--- {label} UMQ type12 batch for umq_post start_ts={s} "
                          f"({len(post_entries)} POST entries, {total_lines} lines) ---")
                    for e in post_entries:
                        print(f"  # --- entry #{e['num']} {e['type']} ---")
                        for raw_line in e['_raw']:
                            print(f"  {raw_line.rstrip()}")
                else:
                    print(f"\n--- {label} UMQ type12 no match for umq_post start_ts={s} ---")
        print("===== End of dump =====\n")

    c_epoll_rounds = extract_epoll_rounds(c_epoll)
    s_epoll_rounds = extract_epoll_rounds(s_epoll)
    c_read_rounds = extract_read_rounds(c_read)
    s_read_rounds = extract_read_rounds(s_read)

    c_eff_note = '' if c_eff_seq is None or c_eff_seq == c_write_start_seq else \
        f', effective start={c_eff_seq}'
    print(f"[INFO] [Client] Write={len(c_write)} (display seq {c_write_start_seq}~{c_write_end_seq}{c_eff_note}), "
          f"Epoll={len(c_epoll)}, Read={len(c_read)}")
    s_eff_note = '' if s_eff_seq is None or s_eff_seq == s_write_start_seq else \
        f', effective start={s_eff_seq}'
    print(f"[INFO] [Server] Write={len(s_write)} (display seq {s_write_start_seq}~{s_write_end_seq}{s_eff_note}), "
          f"Epoll={len(s_epoll)}, Read={len(s_read)}")

    client_rows, client_total = build_rows_for_side('Client', c_write_data, c_epoll_rounds, c_read_rounds,
                                                        client_umq_entries if client_umq_entries else None)
    server_rows, server_total = build_rows_for_side('Server', s_write_data, s_epoll_rounds, s_read_rounds,
                                                      server_umq_entries if server_umq_entries else None)

    def _sum_row(rows, label):
        for row in rows:
            if row[1] == label and row[6]:
                return int(row[6])
        return 0

    server_rows.append(('', '', '', '', '', '', '', ''))
    for label in ('UMQ SUM', 'urma SUM', 'ubsocket SUM'):
        total = _sum_row(client_rows, label) + _sum_row(server_rows, label)
        server_rows.append(('', f'Total {label}', '', '', '', '',
                            format_duration(total), f'client + server {label}'))

    _compute_brpc_time(server_rows)

    transmission = client_total - server_total
    server_rows.append(('', '传输时间', '', '', '', '',
                        format_duration(transmission),
                        f'Client等待耗时={client_total} - Server处理耗时={server_total}'))

    writev_start = None
    readv_end = None
    for row in client_rows:
        if row[1] == 'writeV入口' and row[4] and writev_start is None:
            writev_start = int(row[4])
        if row[1] == 'readv结束' and row[5]:
            readv_end = int(row[5])
    if writev_start is not None and readv_end is not None:
        total_time = readv_end - writev_start
        server_rows.append(('', '总耗时', '', '', '', '',
                            format_duration(total_time),
                            f'Client readv结束_end={readv_end} - Client writeV入口_start={writev_start}'))

    # UMQ/URMA 关联
    if client_umq_entries:
        print(f"[INFO] [Client] 从日志中分离到 {len(c_cats['UMQ'])} 行 UMQ，解析出 {len(client_umq_entries)} 条记录")
    if server_umq_entries:
        print(f"[INFO] [Server] 从日志中分离到 {len(s_cats['UMQ'])} 行 UMQ，解析出 {len(server_umq_entries)} 条记录")

    print_table('Client 侧', client_rows)
    print_table('Server 侧', server_rows)

    if args.output_csv:
        write_csv(args.output_csv, client_rows, server_rows)

if __name__ == "__main__":
    main()