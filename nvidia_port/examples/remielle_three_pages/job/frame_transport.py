"""Binary host transport only; scene data and sampled radiance are unchanged."""
import json
import struct
from PIL import Image
import mmap
import tempfile
import shutil
from pathlib import Path

class MappedTransport:
    def __init__(self, packet_bytes, image_size, fallback):
        response_bytes=20+image_size[0]*image_size[1]*4+1048576
        ram=Path('/dev/shm')
        directory=ram if ram.is_dir() and shutil.disk_usage(ram).free>packet_bytes+response_bytes+33554432 else Path(fallback)
        self.directory=tempfile.TemporaryDirectory(prefix='npt-memory-',dir=directory)
        self.paths=[Path(self.directory.name)/name for name in ['request','response']]
        self.maps=[]
        self.files=[]
        for path,length in zip(self.paths,[packet_bytes,response_bytes]):
            f=path.open('w+b');f.truncate(length)
            self.files.append(f);self.maps.append(mmap.mmap(f.fileno(),length))
        self.image_size=image_size

    def write(self, counts, params, geometry, materials):
        target=self.maps[0];offset=0
        for part in [b'NPTFRM01'+struct.pack('<2I',counts[0],counts[2]),params,geometry,materials]:
            view=memoryview(part).cast('B');target[offset:offset+len(view)]=view;offset+=len(view)
        if offset!=len(target):raise ValueError('Mapped frame length changed')

    def receive(self):
        source=self.maps[1];magic,width,height,length=struct.unpack_from('<8s3I',source)
        if magic!=b'NPTIMG01' or (width,height)!=self.image_size or length>1048576:raise ValueError('Invalid mapped image')
        end=20+width*height*4
        image=Image.frombytes('RGB',(width,height),source[20:end],'raw','BGRX')
        return image,json.loads(source[end:end+length])

    def close(self):
        for mapped in self.maps:mapped.close()
        for file in self.files:file.close()
        self.directory.cleanup()

def read_exact(stream, size):
    data = bytearray(size)
    view = memoryview(data)
    offset = 0
    while offset < size:
        count = stream.readinto(view[offset:])
        if not count:
            raise EOFError(f'Renderer output ended at {offset}/{size} bytes')
        offset += count
    return data

def send_frame(stream, counts, params, geometry, materials):
    for part in [b'NPTFRM01'+struct.pack('<2I',counts[0],counts[2]),params, geometry, materials]:
        view = memoryview(part).cast('B')
        while view:
            count = stream.write(view)
            if not count:
                raise BrokenPipeError('Renderer stopped reading frame')
            view = view[count:]
    stream.flush()

def receive_image(stream, expected=(3024,1964)):
    header = read_exact(stream,20)
    magic,width,height,length = struct.unpack('<8s3I',header)
    if magic != b'NPTIMG01' or (width,height) != expected or length > 1048576:
        raise ValueError('Invalid renderer image header')
    pixels = read_exact(stream,width*height*4)
    report = json.loads(read_exact(stream,length))
    # Same B,G,R -> R,G,B conversion as the old PPM writer; no color transform.
    image = Image.frombytes('RGB',(width,height),bytes(pixels),'raw','BGRX')
    return image,report

def save_result(image, png, report, started):
    import time
    tmp = png.with_suffix('.tmp.png')
    image.save(tmp,compress_level=1)
    tmp.replace(png)
    report = dict(report,total_seconds=time.perf_counter()-started)
    path = png.with_suffix('.json')
    tmp = path.with_suffix('.tmp.json')
    tmp.write_text(json.dumps(report,indent=2))
    tmp.replace(path)
    return report
