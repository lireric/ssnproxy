# ssnproxy
proxy server between serial and TCP interfaces

##Allowed command line options:
	--help                                produce help message
	--port arg (=10001)                   ssnproxy listening TCP port
	--boud arg (=57600)                   serial port boud rate
	--serial arg (=/dev/ttyUSB0)          used COM port
	--wsurl arg (=http://localhost/ssn/data.php)	SSN data web service URL

###Description
ssnproxy starts listen on TCP and serial ports. If message with SSN packet arrived to one of interfaces it transfer to another and waiting for response. If response not arrived in 2 seconds reply timeout response message.
Request can by HTTP REST POST/GET-like format with SSN-type body or plain text stream with SSN-type data - ssnproxy select only SSN-formatted data skipping any headers.
