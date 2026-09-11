#!/usr/bin/env python3
"""Check PCP2 length validation and actual UTF-16 cue-comment conversion."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
source = (repo / 'src/library/rekordbox/rekordboxfeature.cpp').read_text()
start = source.index('QString fromUtf16BeString(')
end = source.index('\n}\n', start) + 3
helper = source[start:end]
driver = r'''
#include "rekordbox_anlz.h"
#include "kaitai/exceptions.h"
#include <QString>
#include <QTextCodec>
#include <iostream>
#include <stdexcept>
'''+helper+r'''
using Entry=rekordbox_anlz_t::cue_extended_entry_t;
void u32(std::string& s, unsigned n) {
 for (int shift=24;shift>=0;shift-=8) s.push_back(static_cast<char>(n>>shift));
}
std::string record(unsigned declared, unsigned commentLength=0) {
 std::string s="PCP2";u32(s,16);u32(s,declared);u32(s,1);
 s.append(24,'\0'); // Remaining fixed fields through loop denominator.
 if(declared>=44)u32(s,commentLength);
 return s;
}
void require(bool condition,const char* what) {
 if(!condition)throw std::runtime_error(what);
}
int main() {
 // The real import helper receives raw bytes with KS_STR_ENCODING_NONE.
 require(fromUtf16BeString("").isEmpty(),"absent comment");
 require(fromUtf16BeString(std::string("\0\0",2)).isEmpty(),"empty comment");
 require(fromUtf16BeString(std::string("\0A\0\0",4))=="A","ASCII comment");
 require(fromUtf16BeString(std::string("\0C\0a\0f\0\xe9\0 \xd8\x3c\xdf\xa7\0\0",16))
   ==QString::fromUtf8(u8"Café 🎧"),"Unicode comment");
 std::cout<<"4 production UTF-16 comment helper cases passed\n";
 // Enough following bytes exist: rejection must not merely be an EOF error.
 for(unsigned declared : {0u,12u,39u}) {
  auto bytes=record(declared)+std::string(128,'X');kaitai::kstream stream(bytes);
  bool rejected=false;
  try {Entry entry(&stream);}catch(const kaitai::validation_failed_error&) {rejected=true;}
  require(rejected,"accepted entry shorter than fixed fields");
  require(stream.pos()==12,"short length rejected too late");
 }
 for(auto lengths : {std::pair<unsigned,unsigned>{44,4},{46,4},{48,6}}) {
  auto bytes=record(lengths.first,lengths.second)+std::string(128,'X');
  kaitai::kstream stream(bytes);bool rejected=false;
  try {Entry entry(&stream);}catch(const kaitai::validation_failed_error&) {rejected=true;}
  require(rejected,"accepted comment extending beyond entry");
  require(stream.pos()==44,"comment crossed boundary before rejection");
 }
 std::cout<<"6 malformed-length cases rejected before payload consumption\n";
 std::string joined=record(40)+record(44)+record(48,4)+std::string("\0A\0\0",4)
    +record(52,4)+std::string("\0A\0\0",4)+std::string("\x09\x14\x28\x3c",4);
 kaitai::kstream stream(joined);unsigned end=0;
 for(unsigned length : {40u,44u,48u,52u}) {
  Entry entry(&stream);end+=length;
  require(stream.pos()==end,"next record alignment");
  require(fromUtf16BeString(entry.comment())==(length<48?QString():QString("A")),"parsed label");
 }
 std::cout<<"4 adjacent valid entries retained alignment and labels\n";
}
'''
with tempfile.TemporaryDirectory(prefix='bitedj-cue-bounds-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(driver)
    qt = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'Qt6Core', 'Qt6Core5Compat'], text=True).split()
    subprocess.run(['c++', '-std=c++17', '-fPIC', '-DKS_STR_ENCODING_NONE',
        '-I'+str(repo/'lib/rekordbox-metadata'), '-I'+str(repo/'lib/kaitai'), str(p/'test.cpp'),
        str(repo/'lib/rekordbox-metadata/rekordbox_anlz.cpp'), str(repo/'lib/kaitai/kaitai/kaitaistream.cpp'),
        *qt, '-lz', '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
