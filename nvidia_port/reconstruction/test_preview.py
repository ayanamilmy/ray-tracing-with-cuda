#!/usr/bin/env python3
"""Read-only reuse of a prepared animation job; writes only NPT_OUTPUT_DIR.

NPT_TEST_FRAMES are original 30-fps timeline indices. Gaps send RESET to the
new backend. This harness never restarts or edits a production worker.
"""
from pathlib import Path
import os
import sys
job=Path(sys.argv[1]).resolve()
source=job.read_text()
source=source.replace("D=Path(__file__).resolve().parent;",f"D=Path({str(job.parent)!r});")
source=source.replace("(D.parent/'scene-preview-504x328.npt')","(Path(os.environ.get('NPT_TEST_SCENE',str(D.parent/'scene-preview-504x328.npt'))))")
source=source.replace("(504,328),out", "(int(os.environ.get('NPT_OUTPUT_WIDTH','504')),int(os.environ.get('NPT_OUTPUT_HEIGHT','328'))),out")
source=source.replace("'1','262144'","os.environ.get('NPT_PASSES','1'),'262144'")
source=source.replace("frames=[int(f) for f in os.environ.get('NPT_BENCH_FRAMES','0,121,571').split(',')]", "frames=[int(f) for f in os.environ.get('NPT_TEST_FRAMES','60,61,62').split(',')]")
source=source.replace("str(mapped.paths[1])+'\\n'", "str(mapped.paths[1])+('\\tRESET' if os.environ.get('NPT_RECONSTRUCTION','off')!='off' and (index==0 or frame!=frames[index-1]+1) else '')+'\\n'")
sys.path.insert(0,str(job.parent));sys.argv=[str(job),'0','--benchmark']
exec(compile(source,str(job),'exec'),{'__file__':str(job),'__name__':'__main__'})
