#!/usr/bin/env python3
# A simple "chat" program to demonstrate the use of ZeroMQ interface
# with the modem application.
# Since the modem currently supports only fixed-length frames,
# they are padded with spaces and stripped after receiving.

FRAME_LENGTH = 100

import zmq, threading, struct
ctx = zmq.Context()

rx = ctx.socket(zmq.SUB)
rx.connect("tcp://localhost:43300")
rx.setsockopt(zmq.SUBSCRIBE, b"")

tx = ctx.socket(zmq.PUB)
tx.connect("tcp://localhost:43301")

def rx_loop():
	while True:
		rxmsg = rx.recv()
		# Parse metadata from the message
		metadata = struct.unpack('IIQIffffffffffI', rxmsg[0:64])
		timestamp = metadata[2]
		data = rxmsg[64:]
		# Print it with a timestamp
		print("\033[034m%10.2f s: \033[033m%s\033[0m" % (1e-9 * timestamp, data.decode('utf-8','ignore').strip()))

rxthread = threading.Thread(target=rx_loop, daemon=True)
rxthread.start()

while True:
	txtext = input()
	txpayload = txtext.encode('utf-8','ignore')

	# Truncate a too long payload, pad a too short one.
	if len(txpayload) >= FRAME_LENGTH:
		txframe = txpayload[0:FRAME_LENGTH]
	else:
		txframe = txpayload + b' ' * (FRAME_LENGTH-len(txpayload))

	# Add metadata and send it
	tx.send(struct.pack('IIQIffffffffffI', 0, 0, 0, 0, 0,0,0,0, 0,0,0,0,0,0, len(txframe)) + txframe)
