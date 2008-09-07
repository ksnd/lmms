/*
 * remote_plugin.h - base class providing RPC like mechanisms
 *
 * Copyright (c) 2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * 
 * This file is part of Linux MultiMedia Studio - http://lmms.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#ifndef _REMOTE_PLUGIN_H
#define _REMOTE_PLUGIN_H

#include "export.h"
#include "midi.h"

#include <vector>
#include <cstring>
#include <string>
#include <cassert>

#ifdef LMMS_BUILD_WIN32

#ifdef LMMS_HAVE_PROCESS_H
#include <process.h>
#endif

#include <Qt/qglobal.h>

#if QT_VERSION >= 0x040400
#include <QtCore/QSharedMemory>
#include <QtCore/QSystemSemaphore>
#else
#error win32-build requires at least Qt 4.4.0
#endif

typedef int32_t key_t;

#else

#define USE_NATIVE_SHMEM

#ifdef LMMS_HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif

#ifdef LMMS_HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif

#endif


#ifdef LMMS_HAVE_SEMAPHORE_H
#include <semaphore.h>
#endif

#ifdef LMMS_HAVE_LOCALE_H
#include <locale.h>
#endif


#ifdef BUILD_REMOTE_PLUGIN_CLIENT
#undef EXPORT
#define EXPORT
#define COMPILE_REMOTE_PLUGIN_BASE
#else
#include <QtCore/QMutex>
#include <QtCore/QProcess>
#endif

// sometimes we need to exchange bigger messages (e.g. for VST parameter dumps)
// so set a usable value here
const int SHM_FIFO_SIZE = 64000;


// implements a FIFO inside a shared memory segment
class shmFifo
{
	// need this union to handle different sizes of sem_t on 32 bit
	// and 64 bit platforms
	union sem32_t
	{
#ifdef LMMS_HAVE_SEMAPHORE_H
		sem_t sem;
#endif
		int semKey;
		char fill[32];
	} ;
	struct shmData
	{
		sem32_t dataSem;	// semaphore for locking this
					// FIFO management data
		sem32_t messageSem;	// semaphore for incoming messages
		volatile int32_t startPtr; // current start of FIFO in memory
		volatile int32_t endPtr;   // current end of FIFO in memory
		char data[SHM_FIFO_SIZE];  // actual data
	} ;

public:
	// constructor for master-side
	shmFifo() :
		m_master( true ),
		m_shmKey( 0 ),
#ifdef USE_NATIVE_SHMEM
		m_shmID( -1 ),
#else
		m_shmObj(),
#endif
		m_data( NULL ),
#ifdef LMMS_BUILD_WIN32
		m_dataSem( QString::null ),
		m_messageSem( QString::null ),
#else
		m_dataSem( NULL ),
		m_messageSem( NULL ),
#endif
		m_lockDepth( 0 )
	{
#ifdef USE_NATIVE_SHMEM
		while( ( m_shmID = shmget( ++m_shmKey, sizeof( shmData ),
					IPC_CREAT | IPC_EXCL | 0600 ) ) == -1 )
		{
		}
		m_data = (shmData *) shmat( m_shmID, 0, 0 );
#else
		do
		{
			m_shmObj.setKey( QString( "%1" ).arg( ++m_shmKey ) );
			m_shmObj.create( sizeof( shmData ) );
		} while( m_shmObj.error() != QSharedMemory::NoError );

		m_data = (shmData *) m_shmObj.data();
#endif
		assert( m_data != NULL );
		m_data->startPtr = m_data->endPtr = 0;
#ifdef LMMS_BUILD_WIN32
		static int k = 0;
		m_data->dataSem.semKey = ( getpid()<<10 ) + ++k;
		m_data->messageSem.semKey = ( getpid()<<10 ) + ++k;
		m_dataSem.setKey( QString::number( m_data->dataSem.semKey ),
						1, QSystemSemaphore::Create );
		m_messageSem.setKey( QString::number(
						m_data->messageSem.semKey ),
						0, QSystemSemaphore::Create );
#else
		m_dataSem = &m_data->dataSem.sem;
		m_messageSem = &m_data->messageSem.sem;

		if( sem_init( m_dataSem, 1, 1 ) )
		{
			fprintf( stderr, "could not initialize m_dataSem\n" );
		}
		if( sem_init( m_messageSem, 1, 0 ) )
		{
			fprintf( stderr, "could not initialize "
							"m_messageSem\n" );
		}
#endif
	}

	// constructor for remote-/client-side - use _shm_key for making up
	// the connection to master
	shmFifo( key_t _shm_key ) :
		m_master( false ),
		m_shmKey( 0 ),
#ifdef USE_NATIVE_SHMEM
		m_shmID( shmget( _shm_key, 0, 0 ) ),
#else
		m_shmObj( QString::number( _shm_key ) ),
#endif
		m_data( NULL ),
#ifdef LMMS_BUILD_WIN32
		m_dataSem( QString::null ),
		m_messageSem( QString::null ),
#else
		m_dataSem( NULL ),
		m_messageSem( NULL ),
#endif
		m_lockDepth( 0 )
	{
#ifdef USE_NATIVE_SHMEM
		if( m_shmID != -1 )
		{
			m_data = (shmData *) shmat( m_shmID, 0, 0 );
		}
#else
		if( m_shmObj.attach() )
		{
			m_data = (shmData *) m_shmObj.data();
		}
#endif
		assert( m_data != NULL );
#ifdef LMMS_BUILD_WIN32
		m_dataSem.setKey( QString::number( m_data->dataSem.semKey ) );
		m_messageSem.setKey( QString::number(
						m_data->messageSem.semKey ) );
#else
		m_dataSem = &m_data->dataSem.sem;
		m_messageSem = &m_data->messageSem.sem;
#endif
	}

	~shmFifo()
	{
#ifdef USE_NATIVE_SHMEM
		shmdt( m_data );
#endif
		// master?
		if( m_master )
		{
#ifdef USE_NATIVE_SHMEM
			shmctl( m_shmID, IPC_RMID, NULL );
#endif
#ifndef LMMS_BUILD_WIN32
			sem_destroy( m_dataSem );
			sem_destroy( m_messageSem );
#endif
		}
	}

	// do we act as master (i.e. not as remote-process?)
	inline bool isMaster( void ) const
	{
		return( m_master );
	}

	// recursive lock
	inline void lock( void )
	{
		if( ++m_lockDepth == 1 )
		{
#ifdef LMMS_BUILD_WIN32
			m_dataSem.acquire();
#else
			sem_wait( m_dataSem );
#endif
		}
	}

	// recursive unlock
	inline void unlock( void )
	{
		if( m_lockDepth > 0 )
		{
			if( --m_lockDepth == 0 )
			{
#ifdef LMMS_BUILD_WIN32
				m_dataSem.release();
#else
				sem_post( m_dataSem );
#endif
			}
		}
	}

	// wait until message-semaphore is available
	inline void waitForMessage( void )
	{
#ifdef LMMS_BUILD_WIN32
		m_messageSem.acquire();
#else
		sem_wait( m_messageSem );
#endif
	}

	// increase message-semaphore
	inline void messageSent( void )
	{
#ifdef LMMS_BUILD_WIN32
		m_messageSem.release();
#else
		sem_post( m_messageSem );
#endif
	}


	inline int32_t readInt( void )
	{
		int32_t i;
		read( &i, sizeof( i ) );
		return( i );
	}

	inline void writeInt( const int32_t & _i )
	{
		write( &_i, sizeof( _i ) );
	}

	inline std::string readString( void )
	{
		const int len = readInt();
		if( len )
		{
			char * sc = new char[len + 1];
			read( sc, len );
			sc[len] = 0;
			std::string s( sc );
			delete[] sc;
			return( s );
		}
		return std::string();
	}


	inline void writeString( const std::string & _s )
	{
		const int len = _s.size();
		writeInt( len );
		write( _s.c_str(), len );
	}


	inline bool messagesLeft( void )
	{
#ifdef LMMS_BUILD_WIN32
		lock();
		const bool empty = ( m_data->startPtr == m_data->endPtr );
		unlock();
		return( !empty );
#else
		int v;
		sem_getvalue( m_messageSem, &v );
		return( v > 0 );
#endif
	}


	inline int shmKey( void ) const
	{
		return( m_shmKey );
	}


private:
	static inline int fastMemCpy( void * _dest, const void * _src,
							const int _len )
	{
		// calling memcpy() for just an integer is obsolete overhead
		if( _len == 4 )
		{
			*( (int32_t *) _dest ) = *( (int32_t *) _src );
		}
		else
		{
			memcpy( _dest, _src, _len );
		}
	}

	void read( void * _buf, int _len )
	{
		lock();
		while( _len > m_data->endPtr - m_data->startPtr )
		{
			unlock();
#ifndef LMMS_BUILD_WIN32
			usleep( 5 );
#endif
			lock();
		}
		fastMemCpy( _buf, m_data->data + m_data->startPtr, _len );
		m_data->startPtr += _len;
		// nothing left?
		if( m_data->startPtr == m_data->endPtr )
		{
			// then reset to 0
			m_data->startPtr = m_data->endPtr = 0;
		}
		unlock();
	}

	void write( const void * _buf, int _len )
	{
		lock();
		while( _len > SHM_FIFO_SIZE - m_data->endPtr )
		{
			// if no space is left, try to move data to front
			if( m_data->startPtr > 0 )
			{
				memmove( m_data->data,
					m_data->data + m_data->startPtr,
					m_data->endPtr - m_data->startPtr );
				m_data->endPtr = m_data->endPtr -
							m_data->startPtr;
				m_data->startPtr = 0;
			}
			unlock();
#ifndef LMMS_BUILD_WIN32
			usleep( 5 );
#endif
			lock();
		}
		fastMemCpy( m_data->data + m_data->endPtr, _buf, _len );
		m_data->endPtr += _len;
		unlock();
	}

	bool m_master;
	key_t m_shmKey;
#ifdef USE_NATIVE_SHMEM
	int m_shmID;
#else
	QSharedMemory m_shmObj;
#endif
	size_t m_shmSize;
	shmData * m_data;
#ifdef LMMS_BUILD_WIN32
	QSystemSemaphore m_dataSem;
	QSystemSemaphore m_messageSem;
#else
	sem_t * m_dataSem;
	sem_t * m_messageSem;
#endif
	int m_lockDepth;

} ;



enum RemoteMessageIDs
{
	IdUndefined,
	IdGeneralFailure,
	IdInitDone,
	IdQuit,
	IdSampleRateInformation,
	IdBufferSizeInformation,
	IdMidiEvent,
	IdStartProcessing,
	IdProcessingDone,
	IdChangeSharedMemoryKey,
	IdChangeInputCount,
	IdChangeOutputCount,
	IdShowUI,
	IdHideUI,
	IdSaveSettingsToString,
	IdSaveSettingsToFile,
	IdLoadSettingsFromString,
	IdLoadSettingsFromFile,
	IdLoadPresetFromFile,
	IdUserBase = 64
} ;



class EXPORT remotePluginBase
{
public:
	struct message
	{
		message() :
			id( IdUndefined ),
			data()
		{
		}

		message( const message & _m ) :
			id( _m.id ),
			data( _m.data )
		{
		}

		message( int _id ) :
			id( _id ),
			data()
		{
		}

		inline message & addString( const std::string & _s )
		{
			data.push_back( _s );
			return( *this );
		}

		message & addInt( int _i )
		{
			char buf[128];
			buf[0] = 0;
			sprintf( buf, "%d", _i );
			data.push_back( std::string( buf ) );
			return( *this );
		}

		message & addFloat( float _f )
		{
			char buf[128];
			buf[0] = 0;
			sprintf( buf, "%f", _f );
			data.push_back( std::string( buf ) );
			return( *this );
		}

		inline std::string getString( int _p = 0 ) const
		{
			return( data[_p] );
		}

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
		inline QString getQString( int _p = 0 ) const
		{
			return( QString::fromStdString( getString( _p ) ) );
		}
#endif

		inline int getInt( int _p = 0 ) const
		{
			return( atoi( data[_p].c_str() ) );
		}

		inline float getFloat( int _p ) const
		{
			return( atof( data[_p].c_str() ) );
		}

		inline bool operator==( const message & _m ) const
		{
			return( id == _m.id );
		}

		int id;

	private:
		std::vector<std::string> data;

		friend class remotePluginBase;

	} ;

	remotePluginBase( shmFifo * _in, shmFifo * _out );
	virtual ~remotePluginBase();

	void sendMessage( const message & _m );
	message receiveMessage( void );

	inline bool messagesLeft( void )
	{
		return( m_in->messagesLeft() );
	}


	message waitForMessage( const message & _m,
						bool _busy_waiting = false );

	inline message fetchAndProcessNextMessage( void )
	{
		message m = receiveMessage();
		processMessage( m );
		return m;
	}

	inline bool fetchAndProcessAllMessages( void )
	{
		while( messagesLeft() )
		{
			fetchAndProcessNextMessage();
		}
	}

	virtual bool processMessage( const message & _m ) = 0;


protected:
	const shmFifo * in( void ) const
	{
		return( m_in );
	}

	const shmFifo * out( void ) const
	{
		return( m_out );
	}


private:
	shmFifo * m_in;
	shmFifo * m_out;

} ;



#ifndef BUILD_REMOTE_PLUGIN_CLIENT

class EXPORT remotePlugin : public remotePluginBase
{
public:
	remotePlugin( const QString & _plugin_executable,
					bool _wait_for_init_done = true );
	virtual ~remotePlugin();

	inline void waitForInitDone( void )
	{
		m_failed = waitForMessage( IdInitDone, TRUE ).id != IdInitDone;
	}

	virtual bool processMessage( const message & _m );

	bool process( const sampleFrame * _in_buf,
					sampleFrame * _out_buf, bool _wait );
	bool waitForProcessingFinished( sampleFrame * _out_buf );

	void processMidiEvent( const midiEvent &, const f_cnt_t _offset );

	void updateSampleRate( sample_rate_t _sr )
	{
		lock();
		sendMessage( message( IdSampleRateInformation ).addInt( _sr ) );
		unlock();
	}

	void showUI( void )
	{
		lock();
		sendMessage( IdShowUI );
		unlock();
	}

	void hideUI( void )
	{
		lock();
		sendMessage( IdHideUI );
		unlock();
	}

	inline bool failed( void ) const
	{
		return( m_failed );
	}


protected:
	inline void lock( void )
	{
		m_commMutex.lock();
	}

	inline void unlock( void )
	{
		m_commMutex.unlock();
	}

	inline void setSplittedChannels( bool _on )
	{
		m_splitChannels = _on;
	}


private:
	void resizeSharedProcessingMemory( void );


	bool m_initialized;
	bool m_failed;

	QProcess m_process;

	QMutex m_commMutex;
	bool m_splitChannels;
#ifdef USE_NATIVE_SHMEM
	int m_shmID;
#else
	QSharedMemory m_shmObj;
#endif
	size_t m_shmSize;
	float * m_shm;

	int m_inputCount;
	int m_outputCount;

} ;

#endif


#ifdef BUILD_REMOTE_PLUGIN_CLIENT

class remotePluginClient : public remotePluginBase
{
public:
	remotePluginClient( key_t _shm_in, key_t _shm_out );
	virtual ~remotePluginClient();

	virtual bool processMessage( const message & _m );

	virtual bool process( const sampleFrame * _in_buf,
					sampleFrame * _out_buf ) = 0;

	virtual void processMidiEvent( const midiEvent &,
						const f_cnt_t /* _offset */ )
	{
	}

	inline float * sharedMemory( void )
	{
		return( m_shm );
	}

	virtual void updateSampleRate( void )
	{
	}

	virtual void updateBufferSize( void )
	{
	}

	inline sample_rate_t sampleRate( void ) const
	{
		return( m_sampleRate );
	}

	inline fpp_t bufferSize( void ) const
	{
		return( m_bufferSize );
	}

	void setInputCount( int _i )
	{
		m_inputCount = _i;
		sendMessage( message( IdChangeInputCount ).addInt( _i ) );
	}

	void setOutputCount( int _i )
	{
		m_outputCount = _i;
		sendMessage( message( IdChangeOutputCount ).addInt( _i ) );
	}

	virtual int inputCount( void ) const
	{
		return( m_inputCount );
	}

	virtual int outputCount( void ) const
	{
		return( m_outputCount );
	}


private:
	void setShmKey( key_t _key, int _size );
	void doProcessing( void );

#ifndef USE_NATIVE_SHMEM
	QSharedMemory m_shmObj;
#endif
	float * m_shm;

	int m_inputCount;
	int m_outputCount;

	sample_rate_t m_sampleRate;
	fpp_t m_bufferSize;

} ;

#endif





#ifdef COMPILE_REMOTE_PLUGIN_BASE

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
#include <QtCore/QCoreApplication>
#endif


remotePluginBase::remotePluginBase( shmFifo * _in, shmFifo * _out ) :
	m_in( _in ),
	m_out( _out )
{
#ifdef LMMS_HAVE_LOCALE_H
	// make sure, we're using common ways to print/scan
	// floats to/from strings (',' vs. '.' for decimal point etc.)
	setlocale( LC_NUMERIC, "C" );
#endif
}




remotePluginBase::~remotePluginBase()
{
	delete m_in;
	delete m_out;
}




void remotePluginBase::sendMessage( const message & _m )
{
	m_out->lock();
	m_out->writeInt( _m.id );
	m_out->writeInt( _m.data.size() );
	int j = 0;
	for( int i = 0; i < _m.data.size(); ++i )
	{
		m_out->writeString( _m.data[i] );
		j += _m.data[i].size();
	}
	m_out->unlock();
	m_out->messageSent();
}




remotePluginBase::message remotePluginBase::receiveMessage( void )
{
	m_in->waitForMessage();
	m_in->lock();
	message m;
	m.id = m_in->readInt();
	const int s = m_in->readInt();
	for( int i = 0; i < s; ++i )
	{
		m.data.push_back( m_in->readString() );
	}
	m_in->unlock();
	return( m );
}




remotePluginBase::message remotePluginBase::waitForMessage(
							const message & _wm,
							bool _busy_waiting )
{
	while( 1 )
	{
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
		if( _busy_waiting && !messagesLeft() )
		{
			QCoreApplication::processEvents(
						QEventLoop::AllEvents, 50 );
			continue;
		}
#endif
		message m = receiveMessage();
		processMessage( m );
		if( m.id == _wm.id )
		{
			return( m );
		}
		else if( m.id == IdGeneralFailure )
		{
			return( m );
		}
	}
}


#endif





#ifdef BUILD_REMOTE_PLUGIN_CLIENT


remotePluginClient::remotePluginClient( key_t _shm_in, key_t _shm_out ) :
	remotePluginBase( new shmFifo( _shm_in ),
				new shmFifo( _shm_out ) ),
#ifndef USE_NATIVE_SHMEM
	m_shmObj(),
#endif
	m_shm( NULL ),
	m_inputCount( 0 ),
	m_outputCount( 0 ),
	m_sampleRate( 44100 ),
	m_bufferSize( 0 )
{
	sendMessage( IdSampleRateInformation );
	sendMessage( IdBufferSizeInformation );
}




remotePluginClient::~remotePluginClient()
{
	sendMessage( IdQuit );

#ifdef USE_NATIVE_SHMEM
	shmdt( m_shm );
#endif
}




bool remotePluginClient::processMessage( const message & _m )
{
	message reply_message( _m.id );
	bool reply = false;
	switch( _m.id )
	{
		case IdGeneralFailure:
			return( false );

		case IdSampleRateInformation:
			m_sampleRate = _m.getInt();
			updateSampleRate();
			break;

		case IdBufferSizeInformation:
			m_bufferSize = _m.getInt();
			updateBufferSize();
			break;

		case IdQuit:
			return( false );

		case IdMidiEvent:
			processMidiEvent(
				midiEvent( static_cast<MidiEventTypes>(
							_m.getInt( 0 ) ),
						_m.getInt( 1 ),
						_m.getInt( 2 ),
						_m.getInt( 3 ) ),
							_m.getInt( 4 ) );
			break;

		case IdStartProcessing:
			doProcessing();
			reply_message.id = IdProcessingDone;
			reply = true;
			break;

		case IdChangeSharedMemoryKey:
			setShmKey( _m.getInt( 0 ), _m.getInt( 1 ) );
			break;

		case IdInitDone:
			break;

		case IdUndefined:
		default:
			fprintf( stderr, "undefined message: %d\n",
							(int) _m.id );
			break;
	}
	if( reply )
	{
		sendMessage( reply_message );
	}

	return( true );
}




void remotePluginClient::setShmKey( key_t _key, int _size )
{
#ifdef USE_NATIVE_SHMEM
	if( m_shm != NULL )
	{
		shmdt( m_shm );
		m_shm = NULL;
	}

	// only called for detaching SHM?
	if( _key == 0 )
	{
		return;
	}

	int shm_id = shmget( _key, _size, 0 );
	if( shm_id == -1 )
	{
		fprintf( stderr, "failed getting shared memory\n" );
	}
	else
	{
		m_shm = (float *) shmat( shm_id, 0, 0 );
	}
#else
	m_shmObj.setKey( QString::number( _key ) );
	if( m_shmObj.attach() )
	{
		m_shm = (float *) m_shmObj.data();
	}
	else
	{
		fprintf( stderr, "failed getting shared memory\n" );
	}
#endif
}




void remotePluginClient::doProcessing( void )
{
	if( m_shm != NULL )
	{
		process( (sampleFrame *)( m_inputCount > 0 ? m_shm : NULL ),
				(sampleFrame *)( m_shm +
					( m_inputCount*m_bufferSize ) ) );
	}
}



#endif

#endif