#!/usr/bin/python

import os
import io
import re
import sys

if __name__ != '__main__':
    raise Exception("This is not a module")

execfile(os.getenv('PYTHONSTARTUP', '/dev/null'))

# inp = sys.stdin.read()
path = sys.argv[1]
DIR = os.path.realpath(os.path.dirname(path))
TOP = os.path.realpath(sys.path[0] + '/../../')
inp = open(path).read()
INP = inp
sio = io.StringIO()

CODE = r'(?P<code>```)'
LINK_INLINE = r'(?:\[(?P<link_inline>(?:[^]]|\\\])+)\]\((?P<link_inline_url>[^)]+)\))'
LINK_REF = r'(?:\[(?P<link_ref>[^]]+)\]\[(?P<link_ref_target>[^]]+)\])'
LINK_ITEM = r'(?:^\[(?P<link_item>[^]]+)\]: (?P<link_item_url>.*)$)'
RE = CODE + '|' + LINK_INLINE + '|' + LINK_REF + '|' + LINK_ITEM

def alter_link_url(url):
    if re.match(r'^(?:(?:http|https|file)://|#)', url):
        return url
    # url path is relative to current file
    global DIR, TOP
    _full_path = os.path.realpath(DIR + '/' + url)
    if _full_path.startswith(TOP):
        return _full_path[len(TOP)+1:]
    return url

_re = re.compile(RE, flags = re.M)

m = _re.search(inp)
while m:
    g = m.groupdict()
    pos = m.start()
    end = m.end()
    sio.write(unicode(inp[:pos]))
    inp = inp[end:]
    if g['code']:
        # a code block
        sio.write(u'```')
        m = re.search('```', inp, flags = re.M)
        end = m.end()
        sio.write(unicode(inp[:end]))
        inp = inp[end:]
    if g['link_inline']:
        g['link_inline_url'] = alter_link_url(g['link_inline_url'])
        sio.write(u'[{link_inline}]({link_inline_url})'.format(**g))
    if g['link_ref']:
        sio.write(u'[{link_ref}][{link_ref_target}]'.format(**g))
    if g['link_item']:
        g['link_item_url'] = alter_link_url(g['link_item_url'])
        sio.write(u'[{link_item}]: {link_item_url}'.format(**g))
    m = _re.search(inp)
sio.write(unicode(inp))

print sio.getvalue()
