/*
 * This file is part of the SSN project.
 *
 * Copyright (C) 2014-2015 Ernold Vasiliev <ericv@mail.ru>
 *

    SSN project is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    SSN project is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with SSN project.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * ssnproxy.cpp
 *
 *  Created on: 4.04.2014 г.
 *      Author: Ernold V.
 */

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/thread.hpp>
#include <boost/format.hpp>
#include <boost/utility.hpp>
#include <boost/chrono.hpp>
#include <boost/asio/write.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/steady_timer.hpp>
#include "AsyncSerial.h"

#include "crc.h"
#include "../../SSN2/inc/ssn_defs.h" // include SSN common definitions:

using namespace std;
namespace po = boost::program_options;

//#define SSN_TIMEOUT	(5)	// timeout (seconds) --> ssn_profs.h

using boost::asio::ip::tcp;

// forward declaration
class session;
void TimeoutHandlerTCP(const boost::system::error_code& error);

uint16_t	uiTarget_SSNObject;	// target proxing object number
const char* cSSNSTART = "===ssn1";
CallbackAsyncSerial* pSerial = NULL;
session* currentSession = NULL;

// Construct a timer without setting an expire time.
//boost::asio::steady_timer* pcTimer = NULL; // common timer for timeouts processing


std::string cmdDataBuff;	// buffer for interprocess data (actual in LC_SERIAL_DATA_READY or LC_TCP_DATA_READY statuses)

/* Interface interchange statuses */
typedef enum
{
	LC_NONE = 0,				/* not any data */
	LC_SERIAL_CMD_SENT,			/* command sent by serial (waiting response with data) */
	LC_TCP_CMD_SENT, 			/* command sent by TCP (waiting response with data) */
	LC_SERIAL_DATA_READY,		/* data load and ready to sent for serial */
	LC_SERIAL_DATA_ERROR,
	LC_TCP_DATA_READY,
	LC_TCP_DATA_ERROR,
	LC_TIMEOUT_ERROR			/* fire on timeout on any interface */
} eLastCommandStatuses;

int nCurrentStatus = LC_NONE;	// store last command status (ref. eLastCommandStatuses)


uint32_t convHex2d(const char* p) {
	uint32_t v = 0;
	uint8_t i;
	uint8_t nc = 0;
	uint32_t d = 1;
	if (p) {
		for (i = strlen(p) - 1; i >= 0; i--) {
			if (i > 8)	// 32bit restriction
				break;
			if ('0' <= p[i] && p[i] <= '9')	{ nc = p[i] - '0'; } else
			if ('a' <= p[i] && p[i] <= 'f')	{ nc = p[i] - 'a' + 10; } else
			if ('A' <= p[i] && p[i] <= 'F')	{ nc = p[i] - 'A' + 10; } else
			{ break; }
			v += nc * d;
			d *= 0x10;
		}
	}
	return v;
}

uint16_t getTarget_Object() {
	return uiTarget_SSNObject;
}

void setTarget_Object(uint16_t nObj) {
	uiTarget_SSNObject = nObj;
}

CallbackAsyncSerial* getSerialClass() {
	return pSerial;
}

void setSerialClass(CallbackAsyncSerial* newSerial) {
	pSerial = newSerial;
}

void setCurrentStatus(int nStatus) {
	nCurrentStatus = nStatus;
}

int getCurrentStatus() {
	return nCurrentStatus;
}

void setCurrentSession(session* pSession) {
	currentSession = pSession;
}

session* getCurrentSession() {
	return currentSession;
}

std::string getDataBuffer() {
	return cmdDataBuff;
}

void setDataBuffer (std::string newStr) {
	cmdDataBuff = newStr;
}

// ===================================================: ssnPDU
class ssnPDU
{
private:
	char cTmpBuf[mainMAX_MSG_LEN];
	uint8_t nScanCnt;
	uint16_t calcCRC;

	sSSNPDU xSSNPDU;

public:
	ssnPDU()
	  {
		xSSNPDU.buffer = 0;
		xSSNPDU.state = SSN_STATE_INIT;
		nScanCnt = 0;
		calcCRC = 0;
	  }

	~ssnPDU ()
	{
		if (xSSNPDU.buffer) {
			free(xSSNPDU.buffer);
		}
	}

	char* getData() {
		return xSSNPDU.buffer;
	}

	uint16_t getDataSize() {
		return xSSNPDU.nDataSize;
	}

	sSSNPDU* getPDU() {
		return &xSSNPDU;
	}

	void resetState()
	{
		xSSNPDU.state = SSN_STATE_INIT;
		nScanCnt = 0;
	}

// process buffer. Return current status.
	int processBuffer(char* pBuffer) {

		if (pBuffer) {
			unsigned int i;
			xSSNPDU.state = SSN_STATE_INIT;
			nScanCnt = 0;

			for (i = 0; i < strlen(pBuffer); i++) {
				this->processNewChar(pBuffer[i]);
			}
		} else {
			return SSN_STATE_ERROR;
		}
		return xSSNPDU.state;
	}

// process new input char. Return current status.
	int processNewChar(char cChar)
	  {
//		xSSNPDU.uiLastTick = getMainTimerTick();	// to do: check timeouts!

		switch (xSSNPDU.state) {
		case SSN_STATE_INIT:
		case SSN_STATE_ERROR:
		case SSN_STATE_READY:
// check for SSN packet start
			if (cChar == cSSNSTART[nScanCnt]) {
				nScanCnt++;
			} else {
				nScanCnt = 0;
			}
				if (nScanCnt == (strlen(cSSNSTART))) {
					xSSNPDU.state = SSN_STATE_LOADING;
					xSSNPDU.counter = 0;
					memset (xSSNPDU.cSSNBuffer,0,15); // clear buffer
//					cout << "SSN_STATE_LOADING" << endl;
				}
			break;
		case SSN_STATE_LOADING:
			nScanCnt = 0;
			xSSNPDU.cSSNBuffer[xSSNPDU.counter++] = cChar;
			if (xSSNPDU.counter == 4) {
				// get object destination
				cTmpBuf[3] = xSSNPDU.cSSNBuffer[3];
				cTmpBuf[2] = xSSNPDU.cSSNBuffer[2];
				cTmpBuf[1] = xSSNPDU.cSSNBuffer[1];
				cTmpBuf[0] = xSSNPDU.cSSNBuffer[0];
				cTmpBuf[4] = 0;
				xSSNPDU.obj_dest = convHex2d(cTmpBuf);
//				if (xSSNPDU.obj_dest != getTarget_Object()) {
//					// if other destination object cancel loading and begin waiting next packet
//					xSSNPDU.state = SSN_STATE_INIT;
//					nScanCnt = 0;
//				}
			} else {
				if (xSSNPDU.counter == 8) {
					// get source object
					cTmpBuf[3] = xSSNPDU.cSSNBuffer[7];
					cTmpBuf[2] = xSSNPDU.cSSNBuffer[6];
					cTmpBuf[1] = xSSNPDU.cSSNBuffer[5];
					cTmpBuf[0] = xSSNPDU.cSSNBuffer[4];
					cTmpBuf[4] = 0;
					xSSNPDU.obj_src = convHex2d(cTmpBuf);
				} else {
					if (xSSNPDU.counter == 10) {
						// get message type
						cTmpBuf[1] = xSSNPDU.cSSNBuffer[9];
						cTmpBuf[0] = xSSNPDU.cSSNBuffer[8];
						cTmpBuf[2] = 0;
						xSSNPDU.message_type = convHex2d(cTmpBuf);
					} else {
						if (xSSNPDU.counter == 14) {
							// get message length
							cTmpBuf[3] = xSSNPDU.cSSNBuffer[13];
							cTmpBuf[2] = xSSNPDU.cSSNBuffer[12];
							cTmpBuf[1] = xSSNPDU.cSSNBuffer[11];
							cTmpBuf[0] = xSSNPDU.cSSNBuffer[10];
							cTmpBuf[4] = 0;
							xSSNPDU.nDataSize = convHex2d(cTmpBuf);
							// free buffer if needed
							if (xSSNPDU.buffer) {
								free(xSSNPDU.buffer);
							}
							if (xSSNPDU.nDataSize < mainMINMEMORYALLOCATE) {
								xSSNPDU.buffer = (char*)malloc(mainMINMEMORYALLOCATE);
							} else {
								xSSNPDU.buffer = (char*)malloc(xSSNPDU.nDataSize + 1);
							}
							if (!xSSNPDU.buffer) {
								xSSNPDU.state = SSN_STATE_ERROR;
								cout << "\r\nSSN buffer allocation error!";
								nScanCnt = 0;
								break;
							} else {
								xSSNPDU.state = SSN_STATE_DATA;
								xSSNPDU.counter = 0;
//								cout << "SSN_STATE_DATA" << endl;
							}
						}
			}}}
			break;
		case SSN_STATE_DATA:
			if (xSSNPDU.counter < xSSNPDU.nDataSize) {
				xSSNPDU.buffer[xSSNPDU.counter++] = cChar;
			} else {
				if ((xSSNPDU.counter >= xSSNPDU.nDataSize) && (xSSNPDU.counter < xSSNPDU.nDataSize + 4)) {
					// get CRC
					cTmpBuf[xSSNPDU.counter++ - xSSNPDU.nDataSize] = cChar;
					if (xSSNPDU.counter >= xSSNPDU.nDataSize + 4) {
							// all data loaded, check CRC
							cTmpBuf[4] = 0;
							xSSNPDU.crc16 = convHex2d(cTmpBuf);
							calcCRC = crc16((uint8_t*) xSSNPDU.buffer, xSSNPDU.nDataSize);
							if (xSSNPDU.crc16 == calcCRC) {
								xSSNPDU.state = SSN_STATE_READY;
								xSSNPDU.buffer[xSSNPDU.nDataSize] = 0;
								nScanCnt = 0;
							} else {
								xSSNPDU.state = SSN_STATE_ERROR;
								sprintf (cTmpBuf, "\r\nSSN data CRC error! (calc=%04x, msg=%04x)", calcCRC, xSSNPDU.crc16);
								cout << cTmpBuf;
								free(xSSNPDU.buffer);
								nScanCnt = 0;
							}
					}
				}
			}
			break;
		default:
			break;
		}
		return xSSNPDU.state;
	  }

};

// ===================================================: session (TCP)
class session

{
	ssnPDU cssnPDU;
	sSSNPDU* pSSNPDU;
	CallbackAsyncSerial* pSerial;
    boost::asio::steady_timer timer_;

public:

	  session(boost::asio::io_service& io_service)
	  : timer_(io_service), socket_(io_service)

  	  {
	  pSerial = getSerialClass();
	  setCurrentStatus(LC_NONE);
	  setCurrentSession(this);
//	  timer_.expires_at(boost::posix_time::pos_infin);
  	  }

	  tcp::socket& socket()
	  {
		  return socket_;
	  }

	  ~session ()
	  {
		  cout << "\r\n ~session";
		  setCurrentSession(NULL);
	  }


  void start()
  {
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        boost::bind(&session::handle_read, this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
  }

  void responseWrite(std::string tmpBuf)
  {
		std::string respMsg = str(boost::format("HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Close\r\n\n%s\r\n") % tmpBuf.size() % tmpBuf);
		cout << "\r\nresponseWrite: " << respMsg << endl;
//		boost::asio::async_write(socket_,
		if (socket_.is_open()) {
			boost::asio::write(socket_,
				boost::asio::buffer((void*)respMsg.data(), respMsg.size()));
		} else {
			cout << "\r\nresponseWrite: socket error";
		}
  }

  boost::asio::steady_timer* getTimer()
  {
	  return &timer_;
  }

private:


  void handle_read(const boost::system::error_code& error,
      size_t bytes_transferred)
  {
    if (!error)
    {
    	int state = 0;
    	std::string tmpBuf;
    	time_t now = time(0);

    	cout << endl << ctime(&now) << "** Read from TCP: " << data_ << endl;

    	state = cssnPDU.processBuffer(data_);

		if (state == SSN_STATE_READY) {
	    	setCurrentStatus(LC_TCP_CMD_SENT);
			pSSNPDU = cssnPDU.getPDU();
			cout << "*** SSN Packet received by TCP. Destination object = " << boost::format("%4d") % pSSNPDU->obj_dest << endl;
// route message. if destobj = target, then call WS, else send to serial.
			if (pSSNPDU->obj_dest == getTarget_Object()) {

			} else {
				// make new packet:
				tmpBuf = str(boost::format("===ssn1%s%s%4x") % pSSNPDU->cSSNBuffer % pSSNPDU->buffer % pSSNPDU->crc16 );
				//pSerial->writeString("===ssn10001000302003b{\"ssn\":{\"v\":1,\"obj\":1,\"cmd\":\"getdevvals\", \"data\": {\"g\":1}}}39ce");
				pSerial->writeString(tmpBuf); // send message
				cout << "\r\n Message to serial: " << tmpBuf;
				// waiting data from serial or timeout:
				timer_.expires_from_now(boost::chrono::seconds(SSN_TIMEOUT/1000)); // start asinc timer
				// Start an asynchronous wait.
				timer_.async_wait(TimeoutHandlerTCP);
				cout << "\r\n Start an asynchronous wait... ";
				setCurrentSession(this);
				sleep( 1 + SSN_TIMEOUT / 1000 );
			}
		} else {
			responseWrite("{\"status\":\"error\"}");
		}
    }
    else
    {
    	cout << "\r\n handle_read. ERROR = " << error;
    	setCurrentSession(NULL);

    	delete this;
    }
  }

  void handle_write(const boost::system::error_code& error)
  {
    if (!error)
    {
    	cout << "\r\nhandle_write: Response to TCP: " << data_ << endl;
    	socket_.async_read_some(boost::asio::buffer(data_, max_length),
          boost::bind(&session::handle_read, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }
    else
    {
    	cout << "\r\n handle_write: ERROR = " << error;
    	delete this;
    }
  }

  tcp::socket socket_;
  enum { max_length = 10000 };
  char data_[max_length];
};

// ===================================================: server (TCP)
class server
{
public:
  server(boost::asio::io_service& io_service, short port)
    : io_service_(io_service),
      acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
  {
    start_accept();
  }

private:
  void start_accept()
  {
    session* new_session = new session(io_service_);
    acceptor_.async_accept(new_session->socket(),
        boost::bind(&server::handle_accept, this, new_session,
          boost::asio::placeholders::error));
  }

  void handle_accept(session* new_session,
      const boost::system::error_code& error)
  {
    if (!error)
    {
    	cout << "\r\nnew_session->start";
    	new_session->start();
    }
    else
    {
    	cout << "\r\ndelete new_session";
    	delete new_session;
    }
    cout << "\r\nstart_accept";
    start_accept();
  }

  boost::asio::io_service& io_service_;
  tcp::acceptor acceptor_;
};


// ===================================================: ssnSerial
class ssnSerial
{

	ssnPDU cssnPDU;
	int currentState;
//	tcp::socket* pSocket;

public:
	ssnSerial()
	  {
//		cssnPDU = new ssnPDU();
		currentState = SSN_STATE_INIT;
	  }

	void received(const char *data, unsigned int len)
	{
		string s(data,len);
		cout << s;
		for (unsigned int i=0; i < len; i++) {
			currentState = cssnPDU.processNewChar(data[i]);

			if (currentState == SSN_STATE_READY) {
				cout << "\r\n*** SSN Packet received by serial." << endl;

//					setDataBuffer (str(boost::format("HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Close\r\n\n%s\r\n") % cssnPDU.getDataSize() % cssnPDU.getData()) );
					setDataBuffer ((cssnPDU.getData()) );
		//			strcpy(data_, "Ok\r\n");
					setCurrentStatus(LC_SERIAL_DATA_READY);
					// notify about data loading completing
					session* s = getCurrentSession();
					if (s) {
						cout << "\r\n Serial: s->responseWrite";
						s->responseWrite(getDataBuffer());
						s->getTimer()->cancel();
					}
					cout << "\r\n Serial: cssnPDU.resetState";
				cssnPDU.resetState();
			}
		}

	}
};

// ===================================================: main
int main(int argc, char* argv[])
{
	int port;
	int boud;
	int nObj;
	string sWSurl;
	string sSerialPort;

// ----------------------------------------------------: get options
	try {

        po::options_description desc("Allowed options");
        desc.add_options()
            ("help", "produce help message")
            ("port", po::value<int>(&port)->default_value(10001), "ssnproxy listening TCP port")
            ("boud", po::value<int>(&boud)->default_value(57600), "serial port boud rate")
            ("tobj", po::value<int>(&nObj)->default_value(3), "target proxing object number")
            ("serial", po::value<string>(&sSerialPort)->default_value("/dev/ttyUSB0"), "used COM port")
            ("wsurl", po::value<string>(&sWSurl)->default_value("http://localhost/ssn/data.php"), "SSN data web service URL")
        ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            cout << desc << "\n";
            return 0;
        }

        setTarget_Object(nObj);
/*
        if (vm.count("port")) {
        	port = vm["port"].as<int>();
        } else {
            cout << "port was not set.\n";
            return 1;
        }
*/

	}
    catch(exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        cerr << "Exception of unknown type!\n";
    }

  try
  {

		cout<<"SSN proxy starting... "<<endl;
// ----------------------------------------------------: start serial
	    ssnSerial ssnSer;
	  	CallbackAsyncSerial serial( sSerialPort, boud );

	  	setSerialClass(&serial); // share serial class

	  	//Bind the received() member function of the foo instance,
	  	//_1 and _2 are parameter forwarding placeholders
	  	serial.setCallback(boost::bind(&ssnSerial::received,ssnSer,_1,_2));

	  	if (!serial.errorStatus() || serial.isOpen()==true)
	  	{
	  		cout << "Serial port " << sSerialPort << " boud " << boud << " - opened" << endl;

	  	}
//serial.writeString("===ssn10001000302003b{\"ssn\":{\"v\":1,\"obj\":1,\"cmd\":\"getdevvals\", \"data\": {\"g\":1}}}39ce");

//	  	boost::this_thread::sleep(boost::posix_time::seconds(5));
//	  	serial.clearCallback();
/*
	  	for(;;)
	      {
	          if(serial.errorStatus() || serial.isOpen()==false)
	          {
	              cerr<<"Error: serial port unexpectedly closed"<<endl;
	              break;
	          }
	            char c;
	            cin.get(c); //blocking wait for standard input
	            if(c==3) //if Ctrl-C
	            {
                    goto quit;//Ctrl-C + x, quit
	            }
	      }
*/

// ----------------------------------------------------: start TCP listener
	  		cout << "Start TSP server on port " << port << endl;

	        boost::asio::io_service io_service;

	        server s(io_service, port);

	        io_service.run();

// ----------------------------------------------------: quit
//	          quit:
		      serial.close();

  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }


  return 0;
}

void TimeoutHandlerTCP(const boost::system::error_code& error)
{
  if (!error)
  {
	  setCurrentStatus(LC_TIMEOUT_ERROR);

	  session* s = getCurrentSession();
	  if (s) {
		  s->responseWrite("{\"status\":\"timeout\"}");
	  }
  }
}
