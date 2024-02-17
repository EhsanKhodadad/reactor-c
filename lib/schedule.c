/**
 * @file
 * @author Edward A. Lee (eal@berkeley.edu)
 * @copyright (c) 2020-2024, The University of California at Berkeley.
 * License: <a href="https://github.com/lf-lang/reactor-c/blob/main/LICENSE.md">BSD 2-clause</a>
 * @brief Implementation of schedule functions for Lingua Franca programs.
 */

#include "schedule.h"
#include "reactor.h"
#include <string.h> // Defines memcpy.

/**
 * Schedule an action to occur with the specified value and time offset
 * with no payload (no value conveyed).
 * See schedule_token(), which this uses, for details.
 *
 * @param action Pointer to an action on the self struct.
 * @param offset The time offset over and above that in the action.
 * @return A handle to the event, or 0 if no event was scheduled, or -1 for error.
 */
trigger_handle_t lf_schedule(void* action, interval_t offset) {
    return lf_schedule_token((lf_action_base_t*)action, offset, NULL);
}

/**
 * Schedule the specified action with an integer value at a later logical
 * time that depends on whether the action is logical or physical and
 * what its parameter values are. This wraps a copy of the integer value
 * in a token. See schedule_token() for more details.
 *
 * @param action The action to be triggered.
 * @param extra_delay Extra offset of the event release above that in the action.
 * @param value The value to send.
 * @return A handle to the event, or 0 if no event was scheduled, or -1 for error.
 */
trigger_handle_t lf_schedule_int(void* action, interval_t extra_delay, int value) {
    token_template_t* template = (token_template_t*)action;

    // NOTE: This doesn't acquire the mutex lock in the multithreaded version
    // until schedule_value is called. This should be OK because the element_size
    // does not change dynamically.
    if (template->type.element_size != sizeof(int)) {
        lf_print_error("Action type is not an integer. element_size is %zu", template->type.element_size);
        return -1;
    }
    int* container = (int*)malloc(sizeof(int));
    *container = value;
    return _lf_schedule_value(action, extra_delay, container, 1);
}

/**
 * Schedule the specified action with the specified token as a payload.
 * This will trigger an event at a later logical time that depends
 * on whether the action is logical or physical and what its parameter
 * values are.
 *
 * logical action: A logical action has an offset (default is zero)
 * and a minimum interarrival time (MIT), which also defaults to zero.
 * The logical time at which this scheduled event will trigger is
 * the current time plus the offset plus the delay argument given to
 * this function. If, however, that time is not greater than a prior
 * triggering of this logical action by at least the MIT, then the
 * one of two things can happen depending on the policy specified
 * for the action. If the action's policy is DROP (default), then the
 * action is simply dropped and the memory pointed to by value argument
 * is freed. If the policy is DEFER, then the time will be increased
 * to equal the time of the most recent triggering plus the MIT.
 *
 * For the above, "current time" means the logical time of the
 * reaction that is calling this function. Logical actions should
 * always be scheduled within a reaction invocation, never asynchronously
 * from the outside. FIXME: This needs to be checked.
 *
 * physical action: A physical action has all the same parameters
 * as a logical action, but its timestamp will be the larger of the
 * current physical time and the time it would be assigned if it
 * were a logical action.
 *
 * There are three conditions under which this function will not
 * actually put an event on the event queue and decrement the reference count
 * of the token (if there is one), which could result in the payload being
 * freed. In all three cases, this function returns 0. Otherwise,
 * it returns a handle to the scheduled trigger, which is an integer
 * greater than 0.
 *
 * The first condition is that stop() has been called and the time offset
 * of this event is greater than zero.
 * The second condition is that the logical time of the event
 * is greater that the stop time (timeout) that is specified in the target
 * properties or on the command line.
 * The third condition is that the trigger argument is null.
 *
 * @param action The action to be triggered.
 * @param extra_delay Extra offset of the event release above that in the action.
 * @param token The token to carry the payload or null for no payload.
 * @return A handle to the event, or 0 if no event was scheduled, or -1 for error.
 */
trigger_handle_t lf_schedule_token(lf_action_base_t* action, interval_t extra_delay, lf_token_t* token) {
    environment_t* env = action->parent->environment;
    
    LF_CRITICAL_SECTION_ENTER(env);
    int return_value = _lf_schedule(env, action->trigger, extra_delay, token);
    // Notify the main thread in case it is waiting for physical time to elapse.
    lf_notify_of_event(env);
    LF_CRITICAL_SECTION_EXIT(env);
    return return_value;
}

/**
 * Schedule an action to occur with the specified value and time offset with a
 * copy of the specified value. If the value is non-null, then it will be copied
 * into newly allocated memory under the assumption that its size is given in
 * the trigger's token object's element_size field multiplied by the specified
 * length.
 *
 * See schedule_token(), which this uses, for details.
 *
 * @param action Pointer to an action on a self struct.
 * @param offset The time offset over and above that in the action.
 * @param value A pointer to the value to copy.
 * @param length The length, if an array, 1 if a scalar, and 0 if value is NULL.
 * @return A handle to the event, or 0 if no event was scheduled, or -1 for
 *  error.
 */
trigger_handle_t lf_schedule_copy(
        lf_action_base_t* action, interval_t offset, void* value, size_t length
) {
    if (length < 0) {
        lf_print_error(
            "schedule_copy():"
            " Ignoring request to copy a value with a negative length (%zu).",
            length
        );
        return -1;
    }
    if (value == NULL) {
        return lf_schedule_token(action, offset, NULL);
    }
    environment_t* env = action->parent->environment;
    token_template_t* template = (token_template_t*)action;
    if (action == NULL || template->type.element_size <= 0) {
        lf_print_error("schedule: Invalid element size.");
        return -1;
    }
    LF_CRITICAL_SECTION_ENTER(env);
    // Initialize token with an array size of length and a reference count of 0.
    lf_token_t* token = _lf_initialize_token(template, length);
    // Copy the value into the newly allocated memory.
    memcpy(token->value, value, template->type.element_size * length);
    // The schedule function will increment the reference count.
    trigger_handle_t result = _lf_schedule(env, action->trigger, offset, token);
    // Notify the main thread in case it is waiting for physical time to elapse.
    lf_notify_of_event(env);
    LF_CRITICAL_SECTION_EXIT(env);
    return result;
}

/**
 * Variant of schedule_token that creates a token to carry the specified value.
 * The value is required to be malloc'd memory with a size equal to the
 * element_size of the specifies action times the length parameter.
 *
 * See schedule_token(), which this uses, for details.
 *
 * @param action The action to be triggered.
 * @param extra_delay Extra offset of the event release above that in the
 *  action.
 * @param value Dynamically allocated memory containing the value to send.
 * @param length The length of the array, if it is an array, or 1 for a scalar
 *  and 0 for no payload.
 * @return A handle to the event, or 0 if no event was scheduled, or -1 for
 *  error.
 */
trigger_handle_t lf_schedule_value(void* action, interval_t extra_delay, void* value, int length) {
    if (length < 0) {
        lf_print_error(
            "schedule_value():"
            " Ignoring request to schedule an action with a value that has a negative length (%d).",
            length
        );
        return -1;
    }
    return _lf_schedule_value((lf_action_base_t*)action, extra_delay, value, (size_t)length);
}

/**
 * Check the deadline of the currently executing reaction against the
 * current physical time. If the deadline has passed, invoke the deadline
 * handler (if invoke_deadline_handler parameter is set true) and return true.
 * Otherwise, return false.
 *
 * @param self The self struct of the reactor.
 * @param invoke_deadline_handler When this is set true, also invoke deadline
 *  handler if the deadline has passed.
 * @return True if the specified deadline has passed and false otherwise.
 */
bool lf_check_deadline(void* self, bool invoke_deadline_handler) {
    reaction_t* reaction = ((self_base_t*)self)->executing_reaction;
    if (lf_time_physical() > (lf_time_logical(((self_base_t *)self)->environment) + reaction->deadline)) {
        if (invoke_deadline_handler) {
            reaction->deadline_violation_handler(self);
        }
        return true;
    }
    return false;
}
