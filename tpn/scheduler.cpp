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

#include "tpn/scheduler.h"
#include "tpn/exception.h"
#include "tpn/string.h"

namespace tpn
{

Scheduler::Scheduler(void)
{
	
}

Scheduler::~Scheduler(void)
{
	for(	Set<Worker*>::iterator it = mWorkers.begin(); 
		it != mWorkers.end();
		++it)
	{
		Worker *worker = *it;
		delete worker;
	}
}

void Scheduler::schedule(Task *task, unsigned msecs)
{
	Synchronize(this);
	
	schedule(task, Time::Now() + double(msecs)/1000);
}

void Scheduler::schedule(Task *task, const Time &when)
{
	Synchronize(this);
	
	remove(task);
	
	mSchedule[when].insert(task);
	mNextTimes[task] = when;
	
	if(!isRunning()) start();
	notifyAll();
}

void Scheduler::repeat(Task *task, unsigned period)
{
	Synchronize(this);
	
	if(!period)
	{
		remove(task);
		return;
	}
	
	if(!mNextTimes.contains(task))
		schedule(task, period);
	
	mPeriods[task] = period;
}

void Scheduler::remove(Task *task)
{
	Synchronize(this);
	
	Time nextTime;
	if(mNextTimes.get(task, nextTime))
	{
		mSchedule[nextTime].erase(task);
		mNextTimes.erase(task);
	}
	
	mPeriods.erase(task);
}

void Scheduler::clear(void)
{
	Synchronize(this);
	
	mSchedule.clear();
	mNextTimes.clear();
	mPeriods.clear();
}

void Scheduler::run(void)
{
	Synchronize(this);
	
	while(!mSchedule.empty())
	{
		double diff =  mSchedule.begin()->first - Time::Now();
		if(diff > 0.)
		{
			wait(unsigned(diff*1000 + 0.5));
			continue;
		}
		
		const Set<Task*> &set = mSchedule.begin()->second;
		for(Set<Task*>::iterator it = set.begin(); it != set.end(); ++it)
		{
			Task *task = *it;
			launch(task);
			
			mNextTimes.erase(task);
			
			unsigned msecs = 0;
			if(mPeriods.get(task, msecs))
				schedule(task, Time::Now() + double(msecs)/1000);
		}
		
		mSchedule.erase(mSchedule.begin());
	}
}

void Scheduler::launch(Task *task)
{
	Synchronize(this);
	
	Worker *worker;
	if(!mAvailableWorkers.empty())
	{
		worker = *mAvailableWorkers.begin();
		mAvailableWorkers.erase(worker);
		worker->join();
	}
	else {
		worker = new Worker(this);
		mWorkers.insert(worker);
	}
	
	worker->setTask(task);
	worker->start();
}

Scheduler::Worker::Worker(Scheduler *scheduler) :
	mScheduler(scheduler),
	mTask(NULL)
{

}

Scheduler::Worker::~Worker(void)
{
	Synchronize(mScheduler);
	mScheduler->mWorkers.erase(this);
	mScheduler->mAvailableWorkers.erase(this);
}

void Scheduler::Worker::setTask(Task *task)
{
	mTask = task;
}

void Scheduler::Worker::run(void)
{
	if(!mTask) return;
	mTask->run();
	mTask = NULL;
	SynchronizeStatement(mScheduler, mScheduler->mAvailableWorkers.insert(this));
}

}