#! /usr/bin/env python3

# System import
import io
import time

# Local import
from testframework import *  # Decorators, LOG, CHECK, KPI
from test_base import *  # Test common helpers
from palanteer_scripting import *  # Palanteer API

# These tests cover the scripting API.


# Some useful variables
spec_step_continue = EvtSpec(
    thread="Main", events=["Freeze"]
)  # "Before first freeze", "After first freeze", "After second freeze"])
spec_add_fruit = EvtSpec(thread="Control", events=["Add fruit"])
spec_unresolved1 = EvtSpec(
    thread="Control", parent="./Task", events=["Add fruit"]
)  # Bad parent path constraint
spec_unresolved2 = EvtSpec(
    thread="Control", parent="Generation", events=["Add fruit"]
)  # Bad parent name
spec_unresolved3 = EvtSpec(
    thread="Control",
    parent="Iteration",
    events=[
        "Do not exist/Add fruit",
        "Add vegetable",
    ],
)  # Bad event path, bad event name


@prepare_suite("script")
def prepare_script():
    # Build a standard test program
    build_target("testprogram", "USE_PL=1")


@declare_test("script")
def test_locks():
    """Locks"""

    data_configure_events([EvtSpec("synchro"), EvtSpec("Workers synchro")])
    launch_testprogram(
        threadgroup_qty=2
    )  # 2 threads so 2 synchro locks (harcoded names in the test programs)
    events = data_collect_events(timeout_sec=10.0)

    # Categorize events by the filter ID, one per lock
    eventsPerLock = {"synchro": [], "Workers synchro": []}

    for e in events:
        eventsPerLock[e.path[-1]].append(e)

    # Check the events for each lock
    for name, lockEvents in [
        ("synchro", eventsPerLock["synchro"]),
        ("Workers synchro", eventsPerLock["Workers synchro"]),
    ]:
        CHECK(lockEvents, "Some events for '%s' are received" % name)
        CHECK(
            (len(lockEvents) % 3) == 0,
            "The '%s' collected event quantity is a multiple of 3 (wait, ntf, use)"
            % name,
            len(lockEvents),
        )
        i, stat_wakeup = 0, []
        while i < len(lockEvents):
            ntf, wait, use = lockEvents[i + 0], lockEvents[i + 1], lockEvents[i + 2]
            if (
                wait.kind == "lock notified"
            ):  # The order of the wait and notification may be reversed
                wait, ntf = ntf, wait
            CHECK(ntf.kind == "lock notified", "'%s' Lock notify received" % name)
            CHECK(
                wait.kind == "lock wait" and wait.date_ns + wait.value > ntf.date_ns,
                "'%s' Lock wait is ended just after the notification" % name,
            )
            CHECK(
                use.kind == "lock use" and use.date_ns >= wait.date_ns + wait.value,
                "'%s' Lock is used just after the end of waiting" % name,
            )
            stat_wakeup.append(wait.date_ns + wait.value - ntf.date_ns)
            i += 3
        stat_wakeup.sort()
        LOG(
            "Median thread wakeup timing for '%s' is %.1f µs"
            % (name, 0.001 * stat_wakeup[int(len(stat_wakeup) / 2)])
        )
    process_stop()


@declare_test("script")
def test_markers():
    """Markers"""

    LOG("Configure event to capture markers")
    data_configure_events([EvtSpec(["Threading", "important"])])
    launch_testprogram(duration=1)
    events = data_collect_events(wanted=["important", "Threading"], timeout_sec=10.0)
    # print("\n" + "\n".join([str(e) for e in events]))
    CHECK(
        "important" in [e.path[-1] for e in events],
        "We received the marker inside the hierarchical tree",
        "\n".join([str(e) for e in events]),
    )
    CHECK(
        "Threading" in [e.path[-1] for e in events],
        "We received the marker at the root",
    )
    process_stop()


@declare_test("script")
def test_cli():
    """CLI"""

    data_configure_events([])

    for i in range(10):
        launch_testprogram(duration=100, pass_first_freeze_point=True)

        # All CLIs must be known thanks to the pass_first_freeze_point=True
        clis = data_get_known_clis()
        CHECK(len(clis) == 6, "All 6 CLIs are registered", clis)
        redirected_output = io.StringIO()
        debug_print_known_clis(redirected_output)
        CHECK(
            len(redirected_output.getvalue().split("\n")) >= 10,
            "CLIs are dumped properly",
        )
        redirected_output.close()

        # Positive tests
        status, answer = program_cli("get_poetry")
        CHECK(
            status == 0 and answer,
            "CLI without parameter is called successfully",
            status,
            answer,
        )
        status, answer = program_cli(
            "test::parameters first=10 second_param=11.5 third=banana"
        )
        CHECK(
            status == 0
            and ": 10" in answer
            and ": 11.5" in answer
            and ": banana" in answer,
            "CLI with explicit parameters is called successfully",
            status,
            answer,
        )
        status, answer = program_cli(
            "test::parameters first=11 second_param=12.5 third=[[ananas is tasty]]"
        )
        CHECK(
            status == 0
            and ": 11" in answer
            and ": 12.5" in answer
            and ": ananas is tasty" in answer,
            "CLI with long string is called successfully",
            status,
            answer,
        )
        status, answer = program_cli(
            "test::parameters second_param=13.5 third=[[papaya is tasty]] first=12"
        )
        CHECK(
            status == 0
            and ": 12" in answer
            and ": 13.5" in answer
            and ": papaya is tasty" in answer,
            "CLI with unordered parameters is called successfully",
            status,
            answer,
        )
        status, answer = program_cli("test::parameters f=13 second=14.5 thi=orange")
        CHECK(
            status == 0
            and ": 13" in answer
            and ": 14.5" in answer
            and ": orange" in answer,
            "CLI with partially named parameters is called successfully",
            status,
            answer,
        )

        status, answer = program_cli("test::parametersDft")
        CHECK(
            status == 0
            and ": 31415926" in answer
            and ": -3.14159" in answer
            and ": no string provided" in answer,
            "CLI with default parameters is called successfully",
            status,
            answer,
        )
        status, answer = program_cli(
            "test::parametersDft first=14 second_param=15.5 third=lemon"
        )
        CHECK(
            status == 0
            and ": 14" in answer
            and ": 15.5" in answer
            and ": lemon" in answer,
            "CLI with overriden default parameters is called successfully",
            status,
            answer,
        )
        status, answer = program_cli(
            "test::parametersDft first=15 second_param=16.5 third=[[ananas is tasty]]"
        )
        CHECK(
            status == 0
            and ": 15" in answer
            and ": 16.5" in answer
            and ": ananas is tasty" in answer,
            "CLI with multi-word string parameter is called successfully",
            status,
            answer,
        )

        # Negative tests
        status, answer = program_cli("test::parametersDft first=-1000")
        CHECK(
            status != 0
            and "Very negative" in answer
            and "This text will not be erased" in answer,
            "CLI returning error '1' is called successfully with the error",
            status,
            answer,
        )
        status, answer = program_cli("test::parametersDft first=-100")
        CHECK(
            status != 0
            and "Mildly negative" in answer
            and "This text will be erased" not in answer,
            "CLI returning error '2' is called successfully with the error",
            status,
            answer,
        )
        status, answer = program_cli("RRrrrooooooo")
        CHECK(
            status != 0 and "Unknown command" in answer,
            "Unknown CLI properly detected",
            status,
            answer,
        )
        status, answer = program_cli("test::parametersDft first")
        CHECK(
            status != 0 and "has no value" in answer,
            "Missing parameter value properly detected",
            status,
            answer,
        )
        status, answer = program_cli("test::parametersDft f=14")
        CHECK(
            status != 0 and "Ambiguous parameter" in answer,
            "Ambiguous parameter name properly detected",
            status,
            answer,
        )
        status, answer = program_cli("test::parametersDft gaaaah=14")
        CHECK(
            status != 0 and "Unknown parameter" in answer,
            "Unknown parameter name properly detected",
            status,
            answer,
        )
        status, answer = program_cli("test::parametersDft first=14iii4 ")
        CHECK(
            status != 0 and "is not a valid integer" in answer,
            "Invalid integer value properly detected",
            status,
            answer,
        )
        status, answer = program_cli("test::parametersDft second=44.3A")
        CHECK(
            status != 0 and "is not a valid float" in answer,
            "Invalid float value properly detected",
            status,
            answer,
        )
        status, answer = program_cli("test::parameters third=[[my taylor is rich]]")
        CHECK(
            status != 0 and "2 parameters are missing" in answer,
            "Missing parameters properly detected",
            status,
            answer,
        )

    process_stop()


@declare_test("script")
def test_crash_info_collection():
    """Collection of crash information"""

    for i in range(3):
        LOG("Configure crash spec with parent")
        data_configure_events(CRASH_SPEC)
        launch_testprogram(
            "crash-segv", capture_output=(sys.platform != "win32")
        )  # Capturing and crashing does not work well on windows
        LOG("Wait for crash (launched with request for segv after a few iterations)")
        events = data_collect_events(timeout_sec=2.0)
        CHECK(not process_is_running(), "Process is not running anymore")
        CHECK(len(events) == 1, "We received 1 event: the crash parent", len(events))
        process_stop()

        LOG("Configure crash spec without parent")
        data_configure_events(EvtSpec("CRASH Stacktrace/*"))
        launch_testprogram(
            "crash-segv", capture_output=(sys.platform != "win32")
        )  # Capturing and crashing does not work well on windows
        LOG("Wait for crash (launched with request for segv after a few iterations)")
        events = data_collect_events(timeout_sec=5.0)
        CHECK(not process_is_running(), "Process is not running anymore")
        CHECK(
            len(events) > 1,
            "We received multiple events corresponding to each stack trace lines",
            len(events),
        )
        process_stop()


@declare_test("script")
def test_freeze_and_step_continue():
    """Program freeze and step_continue"""

    program_set_freeze_mode(True)
    launch_testprogram(threadgroup_qty=2)

    LOG("Configuring events")
    data_configure_events(spec_step_continue)
    LOG("Wait for frozen Main thread")
    data_collect_events(frozen_threads="Main")
    CHECK(
        "Main" in program_get_frozen_threads(),
        "Main thread is frozen",
        program_get_frozen_threads(),
    )
    CHECK(program_step_continue("Main", timeout_sec=1.0), "Step continue is successful")

    LOG("Collect before first freeze")
    event_values = [e.value for e in data_collect_events(frozen_threads="Main")]
    CHECK("Main" in program_get_frozen_threads(), "Main thread is frozen")
    CHECK(
        "Before first freeze" in event_values, "The event before the freeze is received"
    )
    CHECK(
        "After first freeze" not in event_values,
        "The event after the freeze is not received",
    )
    CHECK(program_step_continue("Main", timeout_sec=1.0), "Step continue is successful")

    LOG("Collect after first freeze")
    event_values = [e.value for e in data_collect_events(frozen_threads="Main")]
    CHECK("Main" in program_get_frozen_threads(), "Main thread is frozen")
    CHECK(
        "After first freeze" in event_values,
        "The event after the freeze is now received",
    )
    CHECK(
        "After second freeze" not in event_values,
        "The event after the 2nd freeze is not received",
    )


@declare_test("script")
def test_heavy_collection_with_multiple_specs():
    """Heavy event collection with multiple EvtSpecs"""

    LOG("Unfreeze")
    data_configure_events(
        [
            spec_add_fruit,
            spec_step_continue,
            spec_unresolved1,
            spec_unresolved2,
            spec_unresolved3,
        ]
    )
    program_set_freeze_mode(False)
    events = data_collect_events(timeout_sec=10.0)
    CHECK(
        "After second freeze" in [e.value for e in events],
        "The event after the 2nd freeze is received",
    )
    addFruits = [e.value for e in events if e.path[-1] == "Add fruit"]
    CHECK(len(addFruits) > 0, "Fruit related events are received")
    LOG(
        "'Add fruit' average duration is %d ns (on %d samples)"
        % (sum(addFruits) / max(1, len(addFruits)), len(addFruits))
    )


@declare_test("script")
def test_getters():
    """Getters"""

    thread_names = data_get_known_threads()
    LOG("%d threads are known:\n%s" % (len(thread_names), ", ".join(thread_names)))
    CHECK(len(thread_names) > 1, "Several threads are known")

    event_kinds = data_get_known_event_kinds()
    LOG("%d kinds of event are known" % len(event_kinds))
    # print(", ".join(["(%s:%s:%s)" % (ek[2], ek[1], ek[0][-1]) for ek in event_kinds]))
    CHECK(
        len(event_kinds) > 100, "More than 100 event kinds are known", len(event_kinds)
    )

    unresolved_event = data_get_unresolved_events()
    CHECK(len(unresolved_event) == 4, "4 events shall be unresolved in this test")
    for spec_id, event_spec, msg in unresolved_event:
        LOG("From spec #%d, %s for event '%s'" % (spec_id, msg, event_spec))

    clis = data_get_known_clis()
    CHECK(len(clis) == 6, "6 CLIs shall be registered")
    for name, param_spec, description in clis:
        LOG("  - %s  %s  : %s" % (name, param_spec, description))


@declare_test("script")
def test_debug_printers():
    """Debug print helpers"""

    for func, text in [
        (debug_print_unresolved_events, "Unresolved events"),
        (debug_print_known_threads, "Known threads"),
        (debug_print_known_event_kinds, "Known event kinds"),
        (debug_print_known_clis, "Known CLIs"),
    ]:
        redirected_output = io.StringIO()
        func(redirected_output)
        CHECK(redirected_output.getvalue(), "%s is non empty" % text)
        # print(redirected_output.getvalue())
        redirected_output.close()
    process_stop()
