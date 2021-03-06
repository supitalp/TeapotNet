/*************************************************************************
 *   Copyright (C) 2011-2013 by Paul-Louis Ageneau                       *
 *   paul-louis (at) ageneau (dot) org                                   *
 *                                                                       *
 *   This file is part of TeapotNet.                                     *
 *                                                                       *
 *   TeapotNet is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU Affero General Public License as      *
 *   published by the Free Software Foundation, either version 3 of      *
 *   the License, or (at your option) any later version.                 *
 *                                                                       *
 *   TeapotNet is distributed in the hope that it will be useful, but    *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the        *
 *   GNU Affero General Public License for more details.                 *
 *                                                                       *
 *   You should have received a copy of the GNU Affero General Public    *
 *   License along with TeapotNet.                                       *
 *   If not, see <http://www.gnu.org/licenses/>.                         *
 *************************************************************************/

#ifndef TPN_TIME_H
#define TPN_TIME_H

#include "tpn/include.h"
#include "tpn/serializable.h"
#include "tpn/thread.h"
#include "tpn/mutex.h"

namespace tpn
{

class Time : public Serializable
{
public:
	static Time Now(void);
	static Time Start(void);
	static uint64_t Milliseconds(void);
	static double StructToSeconds(const struct timeval &tv);
	static double StructToSeconds(const struct timespec &ts);
	static void SecondsToStruct(double secs, struct timeval &tv);
	static void SecondsToStruct(double secs, struct timespec &ts);
	
	Time(void);
	Time(time_t time, int usec = 0);
	Time(const String &str);
	~Time(void);
	
	int hour(void) const;
	int minute(void) const;
	int second(void) const;
	int day(void) const;
	int month(void) const;
	int year(void) const;
	int millisecond(void) const;
	int microsecond(void) const;
	
	String toDisplayDate(void) const;
	String toHttpDate(void) const;
	String toIsoDate(void) const;
	String toIsoTime(void) const;
	time_t toUnixTime(void) const;
	void   toStruct(struct timeval &ts) const;
	void   toStruct(struct timespec &ts) const;
	
	int64_t toMicroseconds(void) const;
	int64_t toMilliseconds(void) const;
	double toSeconds(void) const;
	double toHours(void) const;
	double toDays(void) const;
	
	void addMicroseconds(int64_t usec);
	void addMilliseconds(int64_t msec);
	void addSeconds(double seconds);
	void addHours(double hours);
	void addDays(double days);
	
	Time &operator += (double seconds);
	Time operator + (double seconds) const;
	Time &operator -= (double seconds);
	Time operator - (double seconds) const;
	double operator - (const Time &t) const;
	bool operator < (const Time &t);
	bool operator > (const Time &t);
	bool operator == (const Time &t);
	bool operator != (const Time &t);
	operator time_t(void) const;
	
	// Serializable
	void serialize(Serializer &s) const;
	bool deserialize(Serializer &s);
	bool isNativeSerializable(void) const;
	
	enum SerializationFormat { Timestamp, IsoDate, IsoDateTime };
	void setSerializationFormat(SerializationFormat format);

private:
  	static Mutex TimeMutex;
	static Time StartTime;
	
	void parse(const String &str);
	
	time_t mTime;
	int mUsec;
	
	SerializationFormat mFormat;
};

}

#endif
