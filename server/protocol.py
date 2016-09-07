# protocol.py - Module for the communication protocol for Franklin
# Copyright 2014 Michigan Technological University
# Author: Bas Wijnen <wijnen@debian.org>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import random
from websocketd import log

single = {
	'NACK0': b'\xf0',       # Incorrect packet; please resend.
	'NACK1': b'\x91',       # Incorrect packet; please resend.
	'NACK2': b'\xa2',       # Incorrect packet; please resend.
	'NACK3': b'\xc3',       # Incorrect packet; please resend.
	'ACK0': b'\xc4',        # Packet properly received and accepted; ready for next command.  Reply follows if it should.
	'ACK1': b'\xa5',        # Packet properly received and accepted; ready for next command.  Reply follows if it should.
	'ACK2': b'\x96',        # Packet properly received and accepted; ready for next command.  Reply follows if it should.
	'ACK3': b'\xf7',        # Packet properly received and accepted; ready for next command.  Reply follows if it should.
	'STALL0': b'\x88',      # Packet properly received, but not accepted; don't resend packet unmodified.
	'STALL1': b'\xe9',      # Packet properly received, but not accepted; don't resend packet unmodified.
	'STALL2': b'\xda',      # Packet properly received, but not accepted; don't resend packet unmodified.
	'STALL3': b'\xbb',      # Packet properly received, but not accepted; don't resend packet unmodified.
	'ID': b'\xbc',          # Request/reply printer ID code.
	'DEBUG': b'\xdd',       # Debug message; a nul-terminated message follows (no checksum; no resend).
	'STARTUP': b'\xee',     # Starting up.
	'STALLACK': b'\x8f'     # Clear stall.
	}

command = {
	'SET_UUID': 0x00,
	'GET_UUID': 0x01,
	'LINE': 0x02,
	'SINGLE': 0x03,
	'PROBE': 0x04,
	'RUN_FILE': 0x05,
	'SLEEP': 0x06,
	'SETTEMP': 0x07,
	'WAITTEMP': 0x08,
	'READTEMP': 0x09,
	'READPOWER': 0x0a,
	'SETPOS': 0x0b,
	'GETPOS': 0x0c,
	'READ_GLOBALS': 0x0d,
	'WRITE_GLOBALS': 0x0e,
	'READ_SPACE_INFO': 0x0f,
	'READ_SPACE_AXIS': 0x10,
	'READ_SPACE_MOTOR': 0x11,
	'WRITE_SPACE_INFO': 0x12,
	'WRITE_SPACE_AXIS': 0x13,
	'WRITE_SPACE_MOTOR': 0x14,
	'READ_TEMP': 0x15,
	'WRITE_TEMP': 0x16,
	'READ_GPIO': 0x17,
	'WRITE_GPIO': 0x18,
	'QUEUED': 0x19,
	'READPIN': 0x1a,
	'HOME': 0x1b,
	'FORCE_DISCONNECT': 0x1c,
	'CONNECT': 0x1d,
	'RECONNECT': 0x1e,
	'RESUME': 0x1f,
	'GETTIME': 0x20,
	'SPI': 0x21,
	'ADJUSTPROBE': 0x22,
	}

rcommand = {
	'UUID': 0x40,
	'TEMP': 0x41,
	'POWER': 0x42,
	'POS': 0x43,
	'DATA': 0x44,
	'PIN': 0x45,
	'QUEUE': 0x46,
	'HOMED': 0x47,
	'TIME': 0x48,
	'MOVECB': 0x49,
	'TEMPCB': 0x4a,
	'CONTINUE': 0x4b,
	'LIMIT': 0x4c,
	'TIMEOUT': 0x4d,
	'DISCONNECT': 0x4e,
	'PINCHANGE': 0x4f,
	'UPDATE_TEMP': 0x50,
	'UPDATE_PIN': 0x51,
	'CONFIRM': 0x52,
	'FILE_DONE': 0x53,
	'PARKWAIT': 0x54,
	'CONNECTED': 0x55,
	'PINNAME': 0x56,
	}

parsed = {
	'SYSTEM': 0,
	'PRE_LINE': 1,
	'LINE': 2,
	'PRE_ARC': 3,
	'ARC': 4,
	'GPIO': 5,
	'SETTEMP': 6,
	'WAITTEMP': 7,
	'SETPOS': 8,
	'WAIT': 9,
	'CONFIRM': 10,
	'PARK': 11,
}

mask = [[0xc0, 0xc3, 0xff, 0x09],
	[0x38, 0x3a, 0x7e, 0x13],
	[0x26, 0xb5, 0xb9, 0x23],
	[0x95, 0x6c, 0xd5, 0x43],
	[0x4b, 0xdc, 0xe2, 0x83]]

def new_uuid(uuid = None, string = True):
	if uuid is None:
		uuid = [random.randrange(256) for i in range(16)]
		uuid[7] &= 0x0f
		uuid[7] |= 0x40
		uuid[9] &= 0x3f
		uuid[9] |= 0x80
	if string:
		uuid = ''.join('%02x' % x for x in uuid[:16])
		uuid = uuid[:8] + '-' + uuid[8:12] + '-' + uuid[12:16] + '-' + uuid[16:20] + '-' + uuid[20:32]
	return uuid

def build(packet):
	l = len(packet)
	num = l // 3
	packet += [0] * (num + 1)
	for t in range(num):
		s = t & 7
		for bit in range(5):
			check = 0
			for p in range(3):
				check ^= packet[3 * t + p] & mask[bit][p]
			check ^= s & mask[bit][3]
			check ^= check >> 4
			check ^= check >> 2
			check ^= check >> 1
			packet[l + t]
			if check & 1:
				packet[num + t] ^= 1 << (bit + 3)
	return ''.join(map(chr, packet))

def check(packet):
	num = (len(packet) + 3) // 4
	l = len(packet) - num
	for t in range(num):
		s = packet[l + t]
		if s & 7 != t & 7:
			log('bad index %x %x %x' % (s, l, t))
			return False
		for bit in range(5):
			check = 0
			for p in range(3):
				check ^= (packet[3 * t + p] if 3 * t + p < l + t else 0) & mask[bit][p]
			check ^= s & mask[bit][3]
			check ^= check >> 4
			check ^= check >> 2
			check ^= check >> 1
			if (check & 1) != 0:
				log('bad checksum')
				return False
	return True
