#!/usr/bin/env python3
"""Characterize actual BiteDJ provider timelines using generated audio only.

Requires a completed Linux Ninja build, ffmpeg (libmp3lame/AAC/FLAC), and Python.
Reports decoder range, PCM comparison, and seeks; it does NOT prove native Engine cue
alignment or exact decoded length (opening a source may use container estimates).
"""
import argparse
import json
from pathlib import Path
import subprocess
import struct
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir', type=Path)
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-complete-mp3', action='store_true',
        help='Require gapless CBR/VBR MP3 output to match full FFmpeg decode')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix='bitedj-timeline-') as temp:
        root = Path(temp)
        wav = root / 'source.wav'
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
                        'aevalsrc=0.3*sin(2*PI*440*t)|0.2*sin(2*PI*719*t):s=44100:d=3',
                        '-c:a', 'pcm_s16le', str(wav)], check=True)
        paths = [wav]
        for name, codec in [('cbr.mp3', ['libmp3lame', '-b:a', '192k']),
                            ('vbr.mp3', ['libmp3lame', '-q:a', '2']),
                            ('no-xing.mp3', ['libmp3lame', '-b:a', '192k', '-write_xing', '0']),
                            ('source.m4a', ['aac']), ('source.flac', ['flac'])]:
            path = root / name
            subprocess.run(['ffmpeg', '-v', 'error', '-i', str(wav),
                            '-c:a', *codec, str(path)], check=True)
            paths.append(path)
        report = root / 'report.json'
        command = ['python3', str(repo / 'os/tests/test_engine_cue_origin.py'),
                   str(args.build_dir.resolve()), '--ninja', args.ninja,
                   '--probe-source', str(repo / 'tools/audio_timeline_probe.cpp'),
                   '--probe-arg', str(report)]
        for path in paths:
            command += ['--probe-arg', str(path)]
        subprocess.run(command, check=True)
        rows = json.loads(report.read_text())
        assert len(rows) == len(paths)
        for row in rows:
            assert row['sampleRate'] == 44100 and row['channels'] == 2, row
            assert int(row['firstFrame']) == 0 and row['firstReadFrames'] == 64, row
            assert int(row['endFrame']) == int(row['frameLength']) > 0, row
            assert row['provider'], row
            row['file'] = Path(row['file']).name
        for row in rows:
            if row['file'].endswith(('.wav', '.flac')):
                assert int(row['frameLength']) == 132300, row
        for index, (row, path) in enumerate(zip(rows, paths)):
            decoded = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(path),
                '-map', '0:a:0', '-f', 'f32le', '-c:a', 'pcm_f32le', '-'])
            frame_bytes = 4 * row['channels']
            assert len(decoded) % frame_bytes == 0
            row['ffmpegCliDecodedFrames'] = len(decoded) // frame_bytes
            row['rangeMinusCliFrames'] = int(row['frameLength']) - row['ffmpegCliDecodedFrames']
            info = json.loads(subprocess.check_output(['ffprobe', '-v', 'error',
                '-select_streams', 'a:0', '-show_entries',
                'stream=start_pts,start_time,duration_ts,duration,time_base',
                '-of', 'json', str(path)], text=True))
            row['containerTiming'] = info['streams'][0]
            actual = Path(str(report) + f'.{index}.f32').read_bytes()
            assert len(actual) == int(row['frameLength']) * frame_bytes
            overlap = min(len(actual), len(decoded))
            differences = (abs(a[0] - b[0]) for a, b in zip(
                struct.iter_unpack('=f', actual[:overlap]),
                struct.iter_unpack('=f', decoded[:overlap])))
            row['maxOverlappingPcmDifference'] = max(differences, default=0)
            if args.require_complete_mp3 and row['file'] in ('cbr.mp3', 'vbr.mp3'):
                assert row['rangeMinusCliFrames'] == 0, row
            if not row['file'].endswith('.m4a'):
                assert row['tailSeekMatchesSequential'], row
                assert row['maxOverlappingPcmDifference'] < 0.00001, row
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({'tracks': rows,
            'nativeEngineAlignmentVerified': False,
            'rangeIsFullDecodeCount': False}, indent=2) + '\n')
        print('PASS actual providers: WAV, gapless/untagged MP3, AAC, FLAC; ranges and PCM comparisons.')


if __name__ == '__main__':
    main()
