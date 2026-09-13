from pathlib import Path
import tempfile
import unittest
from engine_media_path import resolve_media

class MediaPath(unittest.TestCase):
    def test_media_from_original_library_context(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);library=root/'Engine Library';library.mkdir()
            media=root/'music';media.mkdir();track=media/'fixture.wav';track.write_bytes(b'fixture')
            r=resolve_media(library,root,'../music/fixture.wav')
            self.assertEqual(r['status'],'resolved')
            self.assertEqual(r['sizeBytes'],'7')
            self.assertEqual(r['path'],str(track))
    def test_boundaries_and_missing(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp);root=base/'usb';library=root/'Engine Library';library.mkdir(parents=True)
            outside=base/'outside.mp3';outside.write_bytes(b'audio')
            (root/'link.mp3').symlink_to(outside)
            cases={'../missing.mp3':'missing','../../outside.mp3':'outside-media-root',
              '../link.mp3':'outside-media-root','C:\\music\\a.mp3':'unsupported-reference',
              '/etc/passwd':'unsupported-reference','bad\x00name':'invalid-reference'}
            for value,expected in cases.items():
                with self.subTest(value=value):self.assertEqual(resolve_media(library,root,value)['status'],expected)
            unrelated=base/'unrelated';unrelated.mkdir()
            with self.assertRaises(ValueError):resolve_media(library,unrelated,'a.mp3')

if __name__=='__main__':unittest.main()
