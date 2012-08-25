#!/usr/bin/env python
#
# Public Domain 2008-2012 WiredTiger, Inc.
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# test_truncate01.py
#       session level operations on tables
#

import wiredtiger, wttest
from helper import confirm_empty,\
    key_populate, value_populate, simple_populate,\
    value_populate_complex, complex_populate
from wtscenario import multiply_scenarios, number_scenarios

# Test session.truncate
#       Simple, one-off tests.
class test_truncate_standalone(wttest.WiredTigerTestCase):

    # Test truncation without URI or cursors specified, or with a URI and
    # either cursor specified, expect errors.
    def test_truncate_bad_args(self):
        uri = 'file:xxx'
        simple_populate(self, uri, 'key_format=S', 10)
        msg = '/either a URI or start/stop cursors/'
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.truncate(None, None, None, None), msg)
        cursor = self.session.open_cursor(uri, None)
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.truncate(uri, cursor, None, None), msg)
        self.assertRaisesWithMessage(wiredtiger.WiredTigerError,
            lambda: self.session.truncate(uri, None, cursor, None), msg)


# Test truncation of an object using its URI.
class test_truncate_uri(wttest.WiredTigerTestCase):
    name = 'test_truncate'
    scenarios = [
        ('file', dict(type='file:')),
        ('table', dict(type='table:'))
    ]

    # Populate an object, truncate it by URI, and confirm it's empty.
    def test_truncate_uri(self):
        uri = self.type + self.name

        # A simple, one-file file or table object.
        simple_populate(self, uri, 'key_format=S', 100)
        self.session.truncate(uri, None, None, None)
        confirm_empty(self, uri)
        self.session.drop(uri, None)

        if self.type == "table:":
            complex_populate(self, uri, 'key_format=S', 100)
            self.session.truncate(uri, None, None, None)
            confirm_empty(self, uri)
            self.session.drop(uri, None)


# XXX
#       Test where the start of the delete is within an append area
#       we're already testing where it's inside the implicit area.
# Test session.truncate.
class test_truncate(wttest.WiredTigerTestCase):
    name = 'test_truncate'

    # Use a small page size because we want to create lots of pages.
    # The underlying table routines don't easily support 8t value types, limit
    # those tests to file objects.
    types = [
        ('file', dict(config='leaf_page_max=512,key_format=',type='file:')),
        ('file', dict(config=\
            'leaf_page_max=512,value_format=8t,key_format=',type='file:')),
        ('table', dict(config='leaf_page_max=512,key_format=',type='table:')),
    ]
    keyfmt = [
        ('integer', dict(keyfmt='i')),
        ('recno', dict(keyfmt='r')),
        ('string', dict(keyfmt='S')),
    ]
    image = [
        ('in-memory', dict(reopen=False,append=False,implicit=False)),
        ('on-disk', dict(reopen=True,append=False,implicit=False)),
        ('on-disk-append', dict(reopen=True,append=True,implicit=False)),
        ('on-disk-append-implicit',
            dict(reopen=True,append=True,implicit=True)),
    ]
    size = [
        ('small', dict(nentries=100,skip=7,search=False)),
        ('small', dict(nentries=100,skip=7,search=True)),
        ('big', dict(nentries=1000,skip=37,search=True)),
    ]

    scenarios = number_scenarios(
        multiply_scenarios('.', types, keyfmt, image, size))

    # Set a cursor and optionally search for the item.
    def initCursor(self, uri, key):
        if key == -1:
            return None
        cursor = self.session.open_cursor(uri, None)
        cursor.set_key(key_populate(cursor, key))

        # Test scenarios where we fully instantiate a cursor as well as where we
        # only set the key.  The key may not exist in a column-store so ignore
        # the flag if implicit is set.
        if self.search and not self.implicit:
            self.assertEqual(cursor.search(), 0)
            self.assertEqual(cursor.get_key(), key_populate(cursor, key))

        return cursor

    # Truncate a range using cursors, and check the results.
    def truncateRangeAndCheck(self, uri, begin, end, expected):
        self.pr('truncateRangeAndCheck: ' + str(begin) + ',' + str(end))
        cur1 = self.initCursor(uri, begin)
        cur2 = self.initCursor(uri, end)
        self.session.truncate(None, cur1, cur2, None)
        if not cur1:
            begin = 1
        else:
            cur1.close()
        if not cur2:
            end = self.nentries - 1
        else:
            cur2.close()

        # If the object should be empty, confirm that.
        if begin == 1 and end == self.nentries - 1:
            confirm_empty(self, uri)
            return

        # Check the expected values against the object.
        cursor = self.session.open_cursor(uri, None)
        for i in range(begin, end + 1):
            expected[key_populate(cursor, i)] = 0
        for k, v in expected.iteritems():
            cursor.set_key(k)
            if v == 0 and \
              cursor.key_format == 'r' and cursor.value_format == '8t':
                cursor.search()
                self.assertEqual(cursor.get_values(), [0])
            elif v == 0:
                self.assertEqual(cursor.search(), wiredtiger.WT_NOTFOUND)
            else:
                cursor.search()
                self.assertEqual(cursor.get_values(), v)
        cursor.close()

    # Test truncation using cursors.
    #
    # For begin and end: -1 means pass None for the cursor arg to truncate.
    # An integer N, with 1 <= N < self.nentries, passes a cursor positioned
    # at that element.
    def test_truncate_cursor(self):
        uri = self.type + self.name
        list = [
            (-1, self.nentries - 1),            # begin to end, begin = None
            (1, -1),                            # begin to end, end = None
            (1, self.nentries - 1),             # begin to end
            (-1, self.nentries - self.skip),    # begin to middle, begin = None
            (1, self.nentries - self.skip),     # begin to middle
            (self.skip, -1),                    # middle to end, end = None
            (self.skip, self.nentries - 1),     # middle to end
            (self.skip,                         # middle to different middle
                self.nentries - self.skip),
            (1, 1),                             # begin to begin
            (self.nentries - 1,                 # end to end
                self.nentries - 1),
            (self.skip, self.skip)              # middle to same middle
            ]

        # Implicit records only apply to column-store.
        if self.implicit and not self.keyfmt == 'r':
            return

        # A simple, one-file file or table object.
        for begin,end in list:
            # We want to test cursor transition to the append list: if
            # self.append is set, append the last rows after we've written
            # the object and re-read it, so we have a mix of an on-disk
            # format and an append list.
            append_count = 0
            if self.append:
                append_count = self.skip + 10
            pop_count = self.nentries - append_count

            # Populate the object.
            simple_populate(self, uri, self.config + self.keyfmt, pop_count)

            # Build a dictionary of what the object should look like and
            # compare against it when we're done.
            expected = {}
            cursor = self.session.open_cursor(uri, None)
            for i in range(1, pop_count):
                expected[key_populate(cursor, i)] = [value_populate(cursor, i)]
            cursor.close()

            # Optionally close and re-open the object to get a disk image.
            if self.reopen:
                self.reopen_conn()

            # Optionally append rows to the object.
            if self.append:
                # Column-store: create implicit records, making sure they span
                # the end of the delete range, in other words, "12" is larger
                # than the "10" we used to set append_count.
                if self.implicit:
                    cursor = self.session.open_cursor(uri, None)
                    for i in range(1, 13):
                        expected[key_populate(cursor, pop_count)] = 0
                        pop_count += 1
                    cursor.close()

                cursor = self.session.open_cursor(uri, None)
                while pop_count < self.nentries:
                    key = key_populate(cursor, pop_count)
                    cursor.set_key(key)
                    val = value_populate(cursor, pop_count)
                    cursor.set_value(val)
                    cursor.insert()

                    expected[key] = [val]
                    pop_count += 1
                cursor.close()

            self.truncateRangeAndCheck(uri, begin, end, expected)
            self.session.drop(uri, None)

        # A complex, multi-file table object.
        if self.type == "table:":
            for begin,end in list:
                complex_populate(
                    self, uri, self.config + self.keyfmt, self.nentries)

                # Build a dictionary of what the object should look like and
                # compare against it when we're done.
                expected = {}
                cursor = self.session.open_cursor(uri, None)
                for i in range(1, self.nentries):
                    expected[key_populate(cursor, i)] = \
                        value_populate_complex(i)
                cursor.close()

                if self.reopen:
                    self.reopen_conn()

                self.truncateRangeAndCheck(uri, begin, end, expected)

                self.session.drop(uri, None)


if __name__ == '__main__':
    wttest.run()
