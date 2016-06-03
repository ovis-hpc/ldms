import sys
import struct
import socket

class LDMSD_Except(Exception):
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)

class LDMSD_Req_Attr(object):
    def __init__(self, attr_id, value):
        self.attr_id = attr_id
        # One is added to account for the terminating zero
        self.attr_len = int(len(value)+1)
        self.attr_value = value
        self.fmt = 'iii' + str(self.attr_len) + 's'
        self.packed = struct.pack(self.fmt, 1, self.attr_id,
                                  self.attr_len, self.attr_value)

    def __len__(self):
        return len(self.packed)

    def pack(self):
        return self.packed

class LDMSD_Request(object):
    CLI = 1
    EXAMPLE = 2
    PRDCR_STATUS = 0x100 + 4
    PRDCR_METRIC_SET = 0x100 + 5
    STRGP_STATUS = 0x200 + 4
    UPDTR_STATUS = 0x300 + 4
    PLUGN_STATUS = 0x500 + 4
    SOM_FLAG = 1
    EOM_FLAG = 2
    message_number = 1
    header_size = 20
    def __init__(self, command, message=None, attrs=None):
        self.message = message
        self.request_size = self.header_size
        if message:
            self.request_size += len(self.message)
        self.message_number = LDMSD_Request.message_number
        # Compute the extra size occupied by the attributes and add it
        # to the request size in the request header
        if attrs:
            for attr in attrs:
                self.request_size += len(attr)
            # Account for size of terminating 0
            self.request_size += 4
        self.request = struct.pack('iiiii', -1,
                                   LDMSD_Request.SOM_FLAG | LDMSD_Request.EOM_FLAG,
                                   self.message_number, command,
                                   self.request_size)
        # Add the attributes after the message header
        if attrs:
            for attr in attrs:
                self.request += attr.pack()
            self.request += struct.pack('i', 0) # terminate list
        # Add any message payload
        if message:
            self.request += message
        self.response = ""
        LDMSD_Request.message_number += 1

    def send(self, socket):
        rc = socket.sendall(bytes(self.request))
        if rc:
            raise LDMSD_Except("Error {0} sending request".format(rc))

    def receive(self, socket):
        self.response = ""
        while True:
            hdr = socket.recv(self.header_size)
            (marker, flags, msg_no, cmd_id, rec_len) = struct.unpack('iiiii', hdr)
            if marker != -1:
                raise LDMSD_Except("Invalid response format")
            data = socket.recv(rec_len - self.header_size)
            self.response += data
            if flags & LDMSD_Request.EOM_FLAG:
                break
        return self.response
