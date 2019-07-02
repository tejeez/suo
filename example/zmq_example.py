#!/usr/bin/env python3
# A simple "chat" program to demonstrate the use of ZeroMQ interface
# with the modem application.
# Since the modem currently supports only fixed-length frames,
# this program uses the first byte of a frame to indicate the
# length of the payload and pads the rest with a constant byte.

FRAME_LENGTH = 223
PAD_BYTE = b'.'

import zmq, threading
ctx = zmq.Context()

rx = ctx.socket(zmq.SUB)
rx.connect("tcp://localhost:43300")
rx.setsockopt(zmq.SUBSCRIBE, b"")

tx = ctx.socket(zmq.PUB)
tx.connect("tcp://localhost:43301")

def rx_loop():
	while True:
		rxframe = rx.recv()
		print(rxframe[1 : 1+rxframe[0]].decode('utf-8','ignore'))

rxthread = threading.Thread(target=rx_loop, daemon=True)
rxthread.start()

while True:
	txtext = input()
	# Reserve space for the length byte
	txpayload = txtext.encode('utf-8','ignore')

	# Truncate a too long payload, pad a too short one.
	# Also add the length byte.
	if len(txpayload) >= FRAME_LENGTH-1:
		txframe = bytes([FRAME_LENGTH-1]) + txpayload[0:FRAME_LENGTH-1]
	else:
		txframe = bytes([len(txpayload)]) + txpayload + \
			PAD_BYTE * (FRAME_LENGTH-1-len(txpayload))

	tx.send(txframe)
