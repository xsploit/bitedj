"""Independent helper-output corpus. Payloads stay raw to retain duplicate keys."""
import json

def cases():
    base={'protocol':'bitedj.engine.import','protocolVersion':1,'schema':'3.0.2',
          'sourceUuid':'synthetic','frameUnit':'audio frames at track sample rate',
          'mediaPathContext':{'libraryDirectory':'/media/synthetic/Engine Library',
                              'relativePathBase':'original Engine Library directory'},
          'tracks':[],'playlists':[]}
    raw=json.dumps(base,separators=(',',':')).encode()
    def added(fragment):return raw[:-1]+b','+fragment+b'}'
    yield 'minimal-valid',raw,True
    yield 'structural-characters-in-string',added(b'"extra":"quotes \\\" braces {} [] colon : comma , slash \\\\"'),True
    yield 'same-key-in-sibling-objects',added(b'"extra":[{"name":1},{"name":2}]'),True
    yield 'key-and-string-value-identical',added(b'"extra":{"name":"name"}'),True
    yield 'escaped-key-without-duplicate',added(b'"extra":{"na\\u006de":1}'),True
    yield 'unicode-names-distinct',added('"extra":{"é":1,"é":2}'.encode()),True
    yield 'unicode-astral-key',added('"extra":{"🎵":1}'.encode()),True
    yield 'trailing-whitespace',raw+b'\r\n\t ',True
    yield 'duplicate-top-level',added(b'"sourceUuid":"different"'),False
    yield 'duplicate-top-level-same-value',added(b'"sourceUuid":"synthetic"'),False
    yield 'escaped-duplicate-top-level',added(b'"source\\u0055uid":"synthetic"'),False
    yield 'nested-duplicate',added(b'"extra":{"name":1,"name":2}'),False
    yield 'nested-escaped-duplicate',added(b'"extra":{"name":1,"na\\u006de":2}'),False
    yield 'astral-escaped-duplicate',added('"extra":{"🎵":1,"\\ud83c\\udfb5":2}'.encode()),False
    yield 'malformed-escape',added(b'"extra":"\\q"'),False
    yield 'truncated',raw[:-1],False
    yield 'two-documents',raw+raw,False
    yield 'trailing-garbage',raw+b'x',False
    yield 'trailing-comma',raw[:-1]+b',}',False
    yield 'nonfinite-number',added(b'"extra":NaN'),False
    yield 'infinite-number',added(b'"extra":Infinity'),False
    yield 'invalid-utf8',added(b'"extra":"\xff"'),False
    yield 'unescaped-control',added(b'"extra":"\x01"'),False
    yield 'escaped-control-valid',added(b'"extra":"\\u0001"'),True
    yield 'leading-zero-number',added(b'"extra":01'),False
    yield 'plus-prefixed-number',added(b'"extra":+1'),False
    yield 'leading-dot-number',added(b'"extra":.5'),False
    yield 'trailing-dot-number',added(b'"extra":1.'),False
    yield 'overflow-number',added(b'"extra":1e999'),False
    yield 'vertical-tab-whitespace',raw+b'\x0b',False
    yield 'formfeed-whitespace',raw+b'\x0c',False
    yield 'raw-newline-in-string',added(b'"extra":"line\nline"'),False
    yield 'empty-output',b'',False
    yield 'wrong-root-array',b'[]',False

if __name__=='__main__':
    import base64
    print(json.dumps([{'name':name,'payloadBase64':base64.b64encode(raw).decode(),
                       'accept':accept} for name,raw,accept in cases()],indent=2))
