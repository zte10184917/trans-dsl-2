//
// Created by Darwin Yuan on 2020/6/13.
//

#include <trans-dsl/sched/action/SchedConcurrent.h>
#include <event/concept/Event.h>
#include <trans-dsl/sched/concept/TransactionContext.h>

TSL_NS_BEGIN

#define IS_WORKING__(actionState) (actionState == State::Working || actionState == State::Stopping)
#define IS_CHILD_WORKING(i) IS_WORKING__(children[i])

///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::startUp(TransactionContext& context) -> Status {
   const auto total = getNumOfActions();
   auto hasWorkingChildren = false;
   for(SeqInt i=0; i < total; i++) {
      auto action = get(i);
      if(action == nullptr) {
         return Result::FATAL_BUG;
      }

      ActionStatus status = action->exec(context);
      if(status.isWorking()) {
         children[i] = State::Working;
         hasWorkingChildren = true;
      } else if(status.isDone()) {
         children[i] = State::Done;
      } else if(status.isFailed()) {
         children[i] = State::Done;
         return status;
      } else {
         return Result::FATAL_BUG;
      }
   }

   return hasWorkingChildren ? Result::CONTINUE : Result::SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::cleanUp_(TransactionContext &context, Status& lastError) -> Status {
   const auto total = getNumOfActions();
   auto hasWorking = false;
   lastError = Result::SUCCESS;
   for(SeqInt i=0; i < total; i++) {
      if(IS_CHILD_WORKING(i)) {
         ActionStatus status = get(i)->stop(context);
         if(status.isWorking()) {
            hasWorking = true;
            children[i] = State::Stopping;
         } else {
            children[i] = State::Done;
            if(status.isFailed()) {
               lastError = status;
            }
         }
      }
   }

   return hasWorking ? Result::CONTINUE : lastError;
}

///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::cleanUp(TransactionContext& context, Status failStatus) -> Status {
   context.reportFailure(failStatus);

   Status lastError{};
   ActionStatus result = cleanUp_(context, lastError);
   state = result.isWorking() ? State::Stopping : State::Done;
   if(result.isWorking()) {
      context.reportFailure(lastError);
   }

   return result.isDone() ? failStatus : Status{result};
}

///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::exec(TransactionContext& context) -> Status {
   if(state != State::Idle) {
      return FATAL_BUG;
   }

   ActionStatus status = startUp(context);
   if(status.isFailed()) {
      context.reportFailure(status);
      return cleanUp(context, status);
   }

   state = status.isWorking() ? State::Working : State::Done;

   return status;
}

///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::hasWorkingChildren(SeqInt from) const {
   const auto total = getNumOfActions();
   for(SeqInt i = from; i<total; i++) {
      if(IS_CHILD_WORKING(i)) {
        return true;
      }
   }

   return false;
}

auto SchedConcurrent::getFinalStatus(TransactionContext& context, Status lastError, bool eventConsumed, bool hasWorkingAction) -> Status {
   if(hasWorkingAction) {
      if(state == State::Working) {
         return cleanUp(context, lastError);
      } else {
         context.reportFailure(lastError);
      }

      return eventConsumed ? Result::CONTINUE : Result::UNKNOWN_EVENT;
   }

   return lastError;
}
///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::handleEvent_(TransactionContext& context, const Event& event) -> Status {
   const auto total = getNumOfActions();
   bool hasWorkingAction = false;
   Status lastError = Result::SUCCESS;

   SeqInt i = 0;
   for(; i < total; ++i) {
      if(!IS_CHILD_WORKING(i)) continue;
      ActionStatus status = get(i)->handleEvent(context, event);
      if(status.isWorking()) {
         hasWorkingAction = true;
      } else {
         children[i] = State::Done;
         if(status.isFailed()) {
            lastError = status;
         }
      }

      if(event.isConsumed()) {
         break;
      }
   }

   if(!hasWorkingAction) {
      hasWorkingAction = hasWorkingChildren(i);
   }

   if(hasWorkingAction) {
      if (ActionStatus(lastError).isFailed() && state == State::Working) {
         ActionStatus status = cleanUp(context, lastError);
         if(status.isFailed()) {
            return status;
         }
      } else {
         context.reportFailure(lastError);
      }

      return event.isConsumed() ? Result::CONTINUE : Result::UNKNOWN_EVENT;
   }

   return lastError;
}

///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::handleEvent(TransactionContext& context, const Event& event) -> Status {
   if(!IS_WORKING__(state)) {
      return Result::FATAL_BUG;
   }

   return handleEvent_(context, event);
}

///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::stop(TransactionContext& context) -> Status {
   if(state != State::Working) {
      return Result::FATAL_BUG;
   }

   return cleanUp(context, Result::SUCCESS);
}

///////////////////////////////////////////////////////////////////////////////
auto SchedConcurrent::kill(TransactionContext& context) -> void {
   if(IS_WORKING__(state)) {
      auto total = getNumOfActions();
      for(SeqInt i = 0; i<total; i++) {
         if(IS_CHILD_WORKING(i)) {
            get(i)->kill(context);
         }
      }

      state = State::Done;
   }
}

TSL_NS_END