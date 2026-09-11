#!/usr/bin/env python3
"""Compile production cue-color helper/parser and test optional RGB tails."""
from pathlib import Path
import struct,subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
source=(repo/'src/library/rekordbox/rekordboxfeature.cpp').read_text()
start=source.index('mixxx::RgbColor::optional_t extendedHotCueColor(')
end=source.index('\n}\n',start)+3
helper=source[start:end]
assert 'extendedHotCueColor(*cueExtendedEntry)' in source
with tempfile.TemporaryDirectory(prefix='bitedj-cue-color-') as directory:
 p=Path(directory)
 driver='''#include "rekordbox_anlz.h"
#include "util/color/rgbcolor.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <new>
'''+helper+'''
int main(int argc,char**argv){
 if(argc!=3)return 2;
 std::ifstream f(argv[1],std::ios::binary);kaitai::kstream stream(&f);
 if(std::string(argv[2])=="short") {
  using Entry=rekordbox_anlz_t::cue_extended_entry_t;
  for(unsigned char fill : {0x00,0xa5,0xff}) {
   alignas(Entry) unsigned char storage[sizeof(Entry)];
   std::memset(storage,fill,sizeof(storage));
   stream.seek(0);
   auto* entry=new(storage) Entry(&stream);
   if(stream.pos()!=40 || entry->time()!=1234 || extendedHotCueColor(*entry))
    throw std::runtime_error("short record mismatch");
   entry->~Entry();
  }
  return 0;
 }
 rekordbox_anlz_t a(&stream);
 auto* tag=static_cast<rekordbox_anlz_t::cue_extended_tag_t*>(a.sections()->at(0)->body());
 auto color=extendedHotCueColor(*tag->cues()->at(0));
 bool expected=std::string(argv[2])=="present";
 if(bool(color)!=expected)throw std::runtime_error("presence mismatch");
 if(color && *color!=mixxx::RgbColor(qRgb(20,40,60)))throw std::runtime_error("RGB mismatch");
}
'''
 (p/'driver.cpp').write_text(driver)
 qt=subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Core','Qt6Gui'],text=True).split()
 subprocess.run(['c++','-std=c++17','-fPIC','-DKS_STR_ENCODING_NONE','-I'+str(repo/'src'),'-I'+str(repo/'lib/rekordbox-metadata'),'-I'+str(repo/'lib/kaitai'),str(p/'driver.cpp'),str(repo/'lib/rekordbox-metadata/rekordbox_anlz.cpp'),str(repo/'lib/kaitai/kaitai/kaitaistream.cpp'),*qt,'-lz','-o',str(p/'test')],check=True)
 short=p/'short.pcp2'
 short.write_bytes(b'PCP2'+struct.pack('>III',16,40,1)+struct.pack('>BBHII',1,0,1000,1234,0)+bytes(8)+struct.pack('>HH',0,0))
 subprocess.run([str(p/'test'),str(short),'short'],check=True)
 count=3
 for comment in [None, '', 'Café 🎧']:
  text=b'' if comment is None else (comment+'\0').encode('utf-16-be')
  for tail in range(6):
   entry=b'PCP2'+struct.pack('>III',16,44+len(text)+tail,1)+struct.pack('>BBHII',1,0,1000,1234,0)+bytes(8)+struct.pack('>HHI',0,0,len(text))+text+bytes([9,20,40,60,77])[:tail]
   section=b'PCO2'+struct.pack('>IIIHH',20,20+len(entry),1,1,0)+entry
   fixture=p/'test.EXT';fixture.write_bytes(b'PMAI'+struct.pack('>II',28,28+len(section))+bytes(16)+section)
   subprocess.run([str(p/'test'),str(fixture),'present' if tail>=4 else 'absent'],check=True);count+=1
 print(f'{count} production parser/helper cases passed (three storage patterns for a 40-byte record; zero-length/empty/Unicode comments, 0–4 color bytes plus remainder)')
