#!/usr/bin/env python3
import os
import csv
import argparse
import subprocess
import re
import bisect
from collections import defaultdict
from typing import Dict, Tuple, List, Optional


def parseSocketLog(filePath: str, fdPattern: str) -> Tuple[Dict[Tuple[str, str], int], Dict[str, str]]:
    ipSocketMap, ipFileMap = {}, {}
    fileName = os.path.basename(filePath)
    pattern = rf'local ip ([\d.]+) port \d+, peer ip ([\d.]+) port \d+, {fdPattern}'
    result = subprocess.run(['grep', 'local', filePath], capture_output=True, text=True)
    for line in result.stdout.splitlines():
        match = re.search(pattern, line)
        if match:
            localIp, peerIp, fd = match.group(1), match.group(2), int(match.group(3))
            ipSocketMap[(localIp, peerIp)] = fd
            ipFileMap[localIp] = fileName
    return ipSocketMap, ipFileMap


def filterSeq(logFile: str) -> Dict[int, set]:
    data = defaultdict(set)
    with open(logFile, 'r', encoding='utf-8') as f:
        for line in f:
            if 'is_first: 1' not in line:
                continue
            isFirstMatch = re.search(r'is_first:\s*(\d+)', line)
            typeMatch = re.search(r'type:\s*(\d+)', line)
            rawSocketMatch = re.search(r'raw_socket:\s*(\d+)', line)
            seqMatch = re.search(r'seq:\s*(\d+)', line)
            if not all([isFirstMatch, typeMatch, rawSocketMatch, seqMatch]):
                continue
            if int(typeMatch.group(1)) in [2, 18, 19]:
                data[int(rawSocketMatch.group(1))].add(int(seqMatch.group(1)))
    return data


def collectData(logPath: str, logType: str, fdPattern: str):
    ipSocketMap, ipFileMap, seqData = {}, {}, defaultdict(set)
    parser = lambda fp: parseSocketLog(fp, fdPattern)
    for root, _, files in os.walk(logPath):
        for file in files:
            if file.endswith('.log'):
                filePath = os.path.join(root, file)
                fileName = os.path.basename(filePath)
                sockMap, fileMap = parser(filePath)
                ipSocketMap.update(sockMap)
                ipFileMap.update(fileMap)
                for fd, seqs in filterSeq(filePath).items():
                    seqData[(fileName, fd)].update(seqs)
    return ipSocketMap, ipFileMap, seqData


def parseWriteLogLine(line: str) -> Optional[Dict]:
    pattern = r'\[Write\].*raw_socket:\s*(?P<raw_socket>\d+).*is_first:\s*(?P<is_first>\d+).*seq:\s*(?P<seq>\d+).*type:\s*(?P<type>\d+).*poll_num:\s*(?P<poll_num>\d+).*start_timestamp:\s*(?P<start_timestamp>\d+)'
    match = re.search(pattern, line)
    if match:
        return {
            'raw_socket': int(match.group('raw_socket')),
            'is_first': int(match.group('is_first')),
            'seq': int(match.group('seq')),
            'type': int(match.group('type')),
            'poll_num': int(match.group('poll_num')),
            'start_timestamp': int(match.group('start_timestamp'))
        }
    return None


def parseReadLogLine(line: str) -> Optional[Dict]:
    pattern = r'\[Read\].*raw_socket:\s*(?P<raw_socket>\d+).*seq:\s*(?P<seq>\d+).*type:\s*(?P<type>\d+).*poll_num:\s*(?P<poll_num>\d+).*start_timestamp:\s*(?P<start_timestamp>\d+).*end_timestamp:\s*(?P<end_timestamp>\d+)'
    match = re.search(pattern, line)
    if match:
        return {
            'raw_socket': int(match.group('raw_socket')),
            'seq': int(match.group('seq')),
            'type': int(match.group('type')),
            'poll_num': int(match.group('poll_num')),
            'start_timestamp': int(match.group('start_timestamp')),
            'end_timestamp': int(match.group('end_timestamp'))
        }
    return None


def parseWriteLogFile(filePath: str, targetFd: int, targetTypes: set) -> List[Dict]:
    seqRecords = defaultdict(list)
    with open(filePath, 'r', encoding='utf-8') as f:
        for line in f:
            if 'UBSOCKET PrintSplitTraceInfo' not in line or '[Write]' not in line:
                continue
            parsed = parseWriteLogLine(line)
            if not parsed:
                continue
            if parsed['raw_socket'] != targetFd:
                continue
            if parsed['is_first'] == 1 and parsed['type'] in targetTypes:
                seqRecords[parsed['seq']].append(parsed)
    records = []
    for seq, recs in seqRecords.items():
        lastRec = recs[-1]
        records.append({
            'raw_socket': lastRec['raw_socket'],
            'write_seq': seq,
            'type': lastRec['type'],
            'poll_num': lastRec['poll_num'],
            'start_timestamp': lastRec['start_timestamp']
        })
    return records


def parseReadLogFile(filePath: str, targetFd: int, targetTypes: set) -> List[Dict]:
    seqRecords = defaultdict(list)
    with open(filePath, 'r', encoding='utf-8') as f:
        for line in f:
            if 'UBSOCKET PrintSplitTraceInfo' not in line or '[Read]' not in line:
                continue
            parsed = parseReadLogLine(line)
            if not parsed:
                continue
            if parsed['raw_socket'] != targetFd:
                continue
            if parsed['type'] in targetTypes:
                seqRecords[parsed['seq']].append(parsed)
    records = []
    for seq, recs in seqRecords.items():
        firstRec = recs[0]
        records.append({
            'raw_socket': firstRec['raw_socket'],
            'read_seq': seq,
            'type': firstRec['type'],
            'poll_num': firstRec['poll_num'],
            'start_timestamp': firstRec['start_timestamp'],
            'end_timestamp': firstRec['end_timestamp']
        })
    return records


def calcSocketLatency(clientWriteRecords: List[Dict], serverWriteRecords: List[Dict],
                      clientReadRecords: List[Dict], serverFd: int, clientFd: int) -> List[Tuple]:
    print(
        f'CalLatency ({serverFd}, {clientFd}), {len(clientWriteRecords)=}, {len(serverWriteRecords)=}, {len(clientReadRecords)=}')
    clientWrite = clientWriteRecords
    serverWrite = serverWriteRecords
    clientRead = sorted(clientReadRecords, key=lambda x: x['read_seq'])

    readSeqs = [r['read_seq'] for r in clientRead]
    results = []

    for n, cw in enumerate(clientWrite):
        if n >= len(serverWrite):
            break
        if n + 1 < len(serverWrite):
            serverNSeq = serverWrite[n]['write_seq']
            serverNType = serverWrite[n]['type']
            serverN1Seq = serverWrite[n + 1]['write_seq']
        else:
            serverNSeq = serverWrite[n]['write_seq']
            serverNType = serverWrite[n]['type']
            serverN1Seq = serverWrite[n]['write_seq']

        tmpSeq = serverN1Seq - 1
        idx = bisect.bisect_left(readSeqs, tmpSeq)

        if n == len(clientWrite) - 1:
            if len(clientRead) > 0:
                matchedRead = clientRead[-1]
                seqDiff = matchedRead['read_seq'] - cw['write_seq']
                diffTime = matchedRead['end_timestamp'] - cw['start_timestamp']
                results.append((
                    n + 1, serverFd, clientFd, cw['write_seq'], cw['type'],
                    serverNSeq, serverNType, matchedRead['read_seq'], seqDiff,
                    cw['start_timestamp'], matchedRead['end_timestamp'], diffTime
                ))
        elif idx < len(clientRead):
            matchedRead = clientRead[idx]
            seqDiff = matchedRead['read_seq'] - cw['write_seq']
            diffTime = matchedRead['end_timestamp'] - cw['start_timestamp']
            results.append((
                n + 1, serverFd, clientFd, cw['write_seq'], cw['type'],
                serverNSeq, serverNType, matchedRead['read_seq'], seqDiff,
                cw['start_timestamp'], matchedRead['end_timestamp'], diffTime
            ))

    return results


def writeSocketLatencyResults(filePath: str, results: List[Tuple], includeSorted: bool = True):
    with open(filePath, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['index', 'server_socket_id', 'client_socket_id', 'client_write_seq', 'client_type',
                         'server_write_seq', 'server_type', 'client_read_seq', 'seq_diff',
                         'start_timestamp', 'end_timestamp', 'diff_time'])
        for r in results:
            writer.writerow(r)

        if includeSorted and len(results) > 0:
            f.write('\n=== Sorted by diff_time ===\n')
            writer.writerow(['index', 'server_socket_id', 'client_socket_id', 'client_write_seq', 'client_type',
                             'server_write_seq', 'server_type', 'client_read_seq', 'seq_diff',
                             'start_timestamp', 'end_timestamp', 'diff_time'])
            sortedResults = sorted(results, key=lambda x: x[11])
            for r in sortedResults:
                writer.writerow(r)

            f.write('\n=== P99 seq info ===\n')
            p99Idx = max(0, int(len(sortedResults) * 0.99) - 1)
            p99Record = sortedResults[p99Idx]
            writer.writerow(['index', 'server_socket_id', 'client_socket_id', 'client_write_seq', 'client_type',
                             'server_write_seq', 'server_type', 'client_read_seq', 'seq_diff',
                             'start_timestamp', 'end_timestamp', 'diff_time'])
            writer.writerow(p99Record)


def main():
    parser = argparse.ArgumentParser(
        description='解析 N打N (N>=1) 场景下client与server的socket连接及计算时延P99',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
示例:
  python pick_calc_p99_multi.py --client_path ./client --server_path ./server --output ./result
        '''
    )
    parser.add_argument('--client_path', required=True, help='client日志目录路径')
    parser.add_argument('--server_path', required=True, help='server日志目录路径')
    parser.add_argument('--output', required=True, help='输出结果目录路径')
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    clientIpSocketMap, clientIpFileMap, clientSeqData = collectData(args.client_path, 'client', r'fd (\d+)')
    serverIpSocketMap, serverIpFileMap, serverSeqData = collectData(args.server_path, 'server', r'fd: (\d+)')

    matchedPairs = []
    for (serverLocalIp, serverPeerIp), serverFd in serverIpSocketMap.items():
        clientLocalIp, clientPeerIp = serverPeerIp, serverLocalIp
        if (clientLocalIp, clientPeerIp) in clientIpSocketMap:
            clientFd = clientIpSocketMap[(clientLocalIp, clientPeerIp)]
            serverFile = serverIpFileMap.get(serverLocalIp, 'unknown')
            clientFile = clientIpFileMap.get(clientLocalIp, 'unknown')
            serverSeqCount = len(serverSeqData.get((serverFile, serverFd), set()))
            clientSeqCount = len(clientSeqData.get((clientFile, clientFd), set()))
            matchedPairs.append({
                'server_socket_id': serverFd,
                'client_socket_id': clientFd,
                'server_seq_count': serverSeqCount,
                'client_seq_count': clientSeqCount,
                'server_ip': serverLocalIp,
                'client_ip': clientLocalIp,
                'server_file': serverFile,
                'client_file': clientFile,
                'is_match': serverSeqCount == clientSeqCount
            })

    with open(os.path.join(args.output, 'socket_map.csv'), 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(
            ['server_socket_id', 'client_socket_id', 'server_seq_count', 'client_seq_count', 'is_match', 'server_file',
             'client_file', 'server_ip', 'client_ip'])
        for pair in matchedPairs:
            writer.writerow([pair['server_socket_id'], pair['client_socket_id'], pair['server_seq_count'],
                             pair['client_seq_count'], pair['is_match'], pair['server_file'], pair['client_file'],
                             pair['server_ip'], pair['client_ip']])
    print(f"Socket map results written to {os.path.join(args.output, 'socket_map.csv')}")

    writeTargetTypes = {2, 18, 19}
    readTargetTypes = {3}

    allLatencyResults = []
    for pair in matchedPairs:
        if not pair['is_match']:
            print(
                f"\nWARNING: socket pair ({pair['server_socket_id']}, {pair['client_socket_id']}) is_match is False, skipped")
            continue

        serverFilePath = os.path.join(args.server_path, pair['server_file'])
        clientFilePath = os.path.join(args.client_path, pair['client_file'])

        if not os.path.exists(serverFilePath) or not os.path.exists(clientFilePath):
            print(
                f"\nWARNING: log file not found for socket pair ({pair['server_socket_id']}, {pair['client_socket_id']}), skipped")
            continue

        print(
            f"\n[PARSE] socket pair ({pair['server_socket_id']}, {pair['client_socket_id']}), {clientFilePath=}, {serverFilePath=}")
        clientWriteRecords = parseWriteLogFile(clientFilePath, pair['client_socket_id'], writeTargetTypes)
        serverWriteRecords = parseWriteLogFile(serverFilePath, pair['server_socket_id'], writeTargetTypes)
        clientReadRecords = parseReadLogFile(clientFilePath, pair['client_socket_id'], readTargetTypes)

        results = calcSocketLatency(clientWriteRecords, serverWriteRecords, clientReadRecords,
                                    pair['server_socket_id'], pair['client_socket_id'])

        if len(results) > 0:
            allLatencyResults.extend(results)

            outputFileName = os.path.join(args.output,
                                          f"seq_info_fd_{pair['server_socket_id']}_{pair['client_socket_id']}.csv")
            writeSocketLatencyResults(outputFileName, results)
            sortedResults = sorted(results, key=lambda x: x[11])
            p99Idx = max(0, int(len(sortedResults) * 0.99) - 1)
            p99Record = sortedResults[p99Idx]
            print(
                f"Socket ({pair['server_socket_id']}, {pair['client_socket_id']}) P99: index={p99Record[0]}, server_socket_id={p99Record[1]}, client_socket_id={p99Record[2]}, client_write_seq={p99Record[3]}, client_type={p99Record[4]}, server_write_seq={p99Record[5]}, server_type={p99Record[6]}, client_read_seq={p99Record[7]}, seq_diff={p99Record[8]}, start_timestamp={p99Record[9]}, end_timestamp={p99Record[10]}, diff_time={p99Record[11]}")
    print('')

    if len(allLatencyResults) > 0:
        sortedAllResults = sorted(allLatencyResults, key=lambda x: x[11])
        with open(os.path.join(args.output, 'seq_info_all_fd.csv'), 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow(['index', 'server_socket_id', 'client_socket_id', 'client_write_seq', 'client_type',
                             'server_write_seq', 'server_type', 'client_read_seq', 'seq_diff',
                             'start_timestamp', 'end_timestamp', 'diff_time'])
            for r in sortedAllResults:
                writer.writerow(r)

            f.write('\n=== P99 seq info (all sockets) ===\n')
            writer.writerow(['index', 'server_socket_id', 'client_socket_id', 'client_write_seq', 'client_type',
                             'server_write_seq', 'server_type', 'client_read_seq', 'seq_diff',
                             'start_timestamp', 'end_timestamp', 'diff_time'])
            p99Idx = max(0, int(len(sortedAllResults) * 0.99) - 1)
            p99Record = sortedAllResults[p99Idx]
            writer.writerow((p99Idx + 1,) + p99Record[1:])
        print(
            f"Global P99: index={p99Idx + 1}, server_socket_id={p99Record[1]}, client_socket_id={p99Record[2]}, client_write_seq={p99Record[3]}, client_type={p99Record[4]}, server_write_seq={p99Record[5]}, server_type={p99Record[6]}, client_read_seq={p99Record[7]}, seq_diff={p99Record[8]}, start_timestamp={p99Record[9]}, end_timestamp={p99Record[10]}, diff_time={p99Record[11]}")
        print(f"All latency results sorted and P99 info written to {os.path.join(args.output, 'seq_info_all_fd.csv')}")
    else:
        print("No latency results to write")

    print(f'All outputs written to directory {args.output}')


if __name__ == '__main__':
    main()
