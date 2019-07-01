# On another machine, swap TX and RX frequencies.
# To communicate by ZeroMQ, add parameter: zmq 1
# For LimeSDR (or xtrx), use something like:
# soapy-driver lime txant BAND1 rxant LNAW
../suoapp/suo_soapy txcenter 434e6 rxcenter 434e6 txfreq 433.9e6 rxfreq 433.8e6 framelength 20 tx 1 soapy-driver uhd
