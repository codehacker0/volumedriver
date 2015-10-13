// Copyright 2015 Open vStorage NV
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BACKEND_HEARTBEAT_H_
#define BACKEND_HEARTBEAT_H_

#include "GlobalLockStore.h"
#include "LockCommunicator.h"

#include <youtils/Logging.h>

namespace backend
{

class HeartBeat
{
public:
    using FinishThreadFun = std::function<void()>;
    using TimeDuration = boost::posix_time::time_duration;

    // Careful these are copied *by value* before the thread sees them!
    HeartBeat(GlobalLockStorePtr lock_store,
              FinishThreadFun finish_thread_fun,
              const TimeDuration& heartbeat_timeout,
              const TimeDuration& interrupt_timeout);

    const std::string&
    name() const;

    void
    operator()();

    bool
    grab_lock();

private:
    DECLARE_LOGGER("BackendHeartBeat");

    FinishThreadFun finish_thread_fun_;
    // Only instantiate the communicator when the lock is grabbed??
    LockCommunicator lock_communicator_;
    const TimeDuration heartbeat_timeout_;
};

}

#endif // BACKEND_HEARTBEAT_H_

// Local Variables: **
// mode: c++ **
// End: **
