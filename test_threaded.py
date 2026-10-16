#!/usr/bin/env python3
"""Verify threaded repacking, channel order, boundaries and failure cleanup."""
from collections import defaultdict
from pathlib import Path
import subprocess
import tempfile
import resource
import signal
ROOT = Path(__file__).resolve().parent


def run(name, *args, success=True):
    result = subprocess.run([str(ROOT/name),*map(str,args)],capture_output=True,text=True)
    if (result.returncode == 0) != success:
        raise AssertionError(result.stdout+result.stderr)
    return result


def channel_records(path):
    result = defaultdict(list)
    with path.open('rb') as stream:
        while raw := stream.read(512):
            assert len(raw)==512
            result[raw[8:20]].append(raw)
    return dict(result)


with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    for mode in ('generate','generate-continuous'):
        source = root/(mode+'.mseed')
        run('test_repack',mode,source)
        for encoding in (11,21):
            baseline = root/f'{mode}-{encoding}-1.mseed'
            run('repack',source,baseline,encoding,512,'--threads',1)
            expected = channel_records(baseline)
            for threads in (2,4,8):
                output = root/f'{mode}-{encoding}-{threads}.mseed'
                run('repack',source,output,encoding,512,'--threads',threads)
                run('compare_samples',source,output)
                run('test_repack','verify-continuous' if mode.endswith('continuous') else 'verify',output)
                assert channel_records(output)==expected, 'Per-channel record bytes/order differ'
    source = root/'many.mseed'
    run('test_many_channels',source,4000,'original')
    output = root/'many-threaded.mseed'
    run('repack',source,output,21,512,'--threads',8)
    run('compare_samples',source,output)
    malformed = root/'truncated.mseed'
    malformed.write_bytes((root/'generate-continuous.mseed').read_bytes()[:-1])
    for threads in (2,8):
        failed = root/f'failed-{threads}.mseed'
        run('repack',malformed,failed,21,512,'--threads',threads,success=False)
        assert not failed.exists()
    fractional = root/'fractional.mseed'
    run('test_repack','generate-fractional',fractional)
    failed = root/'fractional-output.mseed'
    run('repack',fractional,failed,21,512,'--threads',4,success=False)
    assert not failed.exists()
    # Force a write failure in a worker without terminating the process.
    def limit_output():
        signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE,(2048,2048))
    failed=root/'write-failure.mseed'
    result=subprocess.run([str(ROOT/'repack'),str(root/'generate-continuous.mseed'),
                           str(failed),'21','512','--threads','4'],
                          preexec_fn=limit_output,capture_output=True,text=True)
    assert result.returncode==1, result.stdout+result.stderr
    assert not failed.exists()
    for option in ('0','65','abc'):
        failed=root/('invalid-'+option)
        run('repack',source,failed,21,512,'--threads',option,success=False)
        assert not failed.exists()
print('PASS threads 1/2/4/8: exact per-channel records, samples/metadata, gaps/overlaps, 4000 channels, failure cleanup')
