#! /usr/bin/env python3

# System import
import time

# Local import
from testframework import *        # Framework (Decorators, LOG, CHECK, KPI)
from test_base import *            # Test common helpers
from palanteer_scripting import *  # Palanteer API

# These tests check the behavior of the Palanteer instrumentation via the test program, depending on some
# build configuration flags


# Useful variables
spec_add_fruit = EvtSpec(thread="Control", events=["Add fruit"])


@declare_test("config instrumentation")
def test_usepl1():
    """Config USE_PL=1"""
    build_target("testprogram", "USE_PL=1")

    data_configure_events([EvtSpec("CRASH"), spec_add_fruit])
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    events = data_collect_events(timeout_sec=1.)
    CHECK(events, "Some events are received")
    CHECK(not [1 for e in events if e.path[-1]=="CRASH Stacktrace"], "No crash event has been received")
    status, answer = program_cli("async_assert condvalue=0")
    CHECK(status==0, "CLI to make an assert called successfully", status, answer)
    events = data_collect_events(timeout_sec=2.)
    CHECK([1 for e in events if e.path[-1]=="CRASH"] and not process_is_running(), "Crash event due to the assert has been received")
    process_stop()


@declare_test("config instrumentation")
def test_usepl0():
    """Config USE_PL=0"""
    build_target("testprogram", "USE_PL=0")

    data_configure_events([])
    try:
        launch_testprogram(connection_timeout_sec=2.)
        CHECK(False, "Connection should have failed")
    except ConnectionError:
        CHECK(True, "No connection, as expected")

    process_stop()


@declare_test("config instrumentation")
def test_usepl1_nocontrol():
    """Config PL_NOCONTROL=1"""
    build_target("testprogram", "USE_PL=1 PL_NOCONTROL=1")

    data_configure_events([CRASH_SPEC, spec_add_fruit])
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    status, answer = program_cli("get_poetry")
    CHECK(status!=0 and "Control" in answer, "CLI to get some poetry failed as expected (no control)")

    events = data_collect_events(timeout_sec=2.)
    addFruits = [e.value for e in events if e.path[-1]=="Add fruit"]
    CHECK(len(addFruits), "Some fruit events are received")
    process_stop()


@declare_test("config instrumentation")
def test_usepl1_noevent():
    """Config PL_NOEVENT=1"""
    build_target("testprogram", "USE_PL=1 PL_NOEVENT=1")

    data_configure_events([CRASH_SPEC, spec_add_fruit])
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    status, answer = program_cli("get_poetry")
    CHECK(status==0 and answer=="To bug, or not to bug,that is the question",
          "CLI to get some poetry called successfully", status, answer)
    CHECK(not data_collect_events(timeout_sec=2.), "No event is received")
    process_stop()


@declare_test("config instrumentation")
def test_usepl1_noassert():
    """Config PL_NOASSERT=1"""
    build_target("testprogram", "USE_PL=1 PL_NOASSERT=1")

    data_configure_events([CRASH_SPEC, spec_add_fruit])
    try:
        launch_testprogram(pass_first_freeze_point=True) # Wait first freeze to get all CLIs
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    status, answer = program_cli("async_assert condvalue=0")
    CHECK(status==0, "CLI to make an assert called successfully", status, answer)
    events = data_collect_events(timeout_sec=2.)
    CHECK(events, "Some events are received")
    CHECK(not [1 for e in events if e.path[-1]=="CRASH Stacktrace"] and process_is_running(), "No crash event has been received because of disabled assertions")
    process_stop()


@declare_test("config instrumentation")
def test_usepl1_saturated_request_buffer():
    """Config PL_IMPL_REMOTE_REQUEST_BUFFER_BYTE_QTY=64"""
    build_target("testprogram", "USE_PL=1 PL_IMPL_REMOTE_REQUEST_BUFFER_BYTE_QTY=64")

    data_configure_events(CRASH_SPEC)
    try:
        launch_testprogram(pass_first_freeze_point=True) # Wait first freeze to get all CLIs
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    status, answer = program_cli("test::parametersDft third=[[hello]]")
    CHECK(status==0, "CLI with small command request size called successfully", status, answer)
    status, answer = program_cli("test::parametersDft third=[[hello, what is the weather today?]]")
    events = data_collect_events(timeout_sec=2.)
    CHECK(status!=0 and not process_is_running() and events, "CLI with too large command request size failed and program stopped (assertion)", status, answer, events)
    process_stop()


@declare_test("config instrumentation")
def test_usepl1_saturated_response_buffer():
    """Config PL_IMPL_REMOTE_RESPONSE_BUFFER_BYTE_QTY=110"""
    build_target("testprogram", "USE_PL=1 PL_IMPL_REMOTE_RESPONSE_BUFFER_BYTE_QTY=110")

    data_configure_events(CRASH_SPEC)
    try:
        launch_testprogram(pass_first_freeze_point=True) # Wait first freeze to get all CLIs
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    status, answer = program_cli("test::parametersDft third=[[hello]]")  # The answer with payload is just less than 110 bytes
    CHECK(status==0, "CLI with small command reponse size called successfully", status, answer)
    status, answer = program_cli("test::parametersDft third=[[hello, what is the weather today?]]")   # The answer with payload exceeds the 110 bytes
    events = data_collect_events(timeout_sec=2.)
    CHECK(status!=0 and "CLI response buffer is full" in answer and process_is_running(),
          "CLI with too large command reponse size failed and program is still running",
          status, answer, process_is_running())
    process_stop()


@declare_test("config instrumentation")
def test_usepl1_saturated_string_buffer():
    """Config PL_IMPL_STRING_BUFFER_BYTE_QTY=128"""
    build_target("testprogram", "USE_PL=1 PL_IMPL_STRING_BUFFER_BYTE_QTY=128")

    data_configure_events([CRASH_SPEC, EvtSpec("test_marker")])
    try:
        launch_testprogram(pass_first_freeze_point=True) # Wait first freeze to get all CLIs
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    status, answer = program_cli("test::marker msg=[[cool]]")
    CHECK(status==0, "CLI creating a marker was called successfully", status, answer)
    events = data_collect_events(wanted="test_marker")
    CHECK(status==0, "The marker has been received successfully", status, answer)

    status, answer = program_cli("test::marker msg=[[%s]]" % ("a"*130))  # Too long a string for the buffer
    events = data_collect_events(wanted="test_marker", timeout_sec=1.)
    CHECK(not process_is_running(), "The process stopped as expected due to a failed assertion");
    process_stop()


@declare_test("config instrumentation")
def test_usepl1_saturated_cli_qty():
    """Config PL_IMPL_MAX_CLI_QTY=5"""
    build_target("testprogram", "USE_PL=1 PL_IMPL_MAX_CLI_QTY=5")

    data_configure_events(spec_add_fruit)
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    events = data_collect_events(timeout_sec=5.)
    CHECK(not process_is_running() and not events, "No event received because an assert failed (max CLI exceeded)",
          process_is_running(), len(events))
    process_stop()


@declare_test("config instrumentation")
def test_usepl1_saturated_cli_param_qty():
    """Config PL_IMPL_CLI_MAX_PARAM_QTY=2"""
    build_target("testprogram", "USE_PL=1 PL_IMPL_CLI_MAX_PARAM_QTY=2")

    data_configure_events(spec_add_fruit)
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    events = data_collect_events(timeout_sec=5.)
    CHECK(not process_is_running() and not events, "No event received because an assert failed (max CLI param exceeded)",
          process_is_running(), len(events))
    process_stop()


@declare_test("config instrumentation")
def test_usepl1_nocontrol_noevent_noassert():
    """Config PL_NOCONTROL=1 PL_NOEVENT=1 PL_NOASSERT=1"""
    build_target("testprogram", "USE_PL=1 PL_NOCONTROL=1 PL_NOEVENT=1 PL_NOASSERT=1")

    data_configure_events([])
    try:
        launch_testprogram(connection_timeout_sec=2.)
        CHECK(False, "Connection should have failed")
    except ConnectionError:
        CHECK(True, "No connection, as expected")

    process_stop()


@declare_test("config instrumentation")
def test_short_hash_string():
    """Config short hash string PL_SHORT_STRING_HASH=1"""
    build_target("testprogram", "USE_PL=1 PL_SHORT_STRING_HASH=1")

    data_configure_events(spec_add_fruit)
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    CHECK(data_collect_events(timeout_sec=2.), "Fruit related events are received")
    process_stop()


@declare_test("config instrumentation")
def test_32bits_arch():
    """Config 32 bits architecture PL_SHORT_STRING_HASH=1 -m32"""
    if sys.platform=="win32":
        LOG("Skipped: 32 bit build is not applicable under Windows")
        return
    build_target("testprogram", "USE_PL=1 PL_IMPL_STACKTRACE=0 PL_SHORT_STRING_HASH=1", compilation_flags=["-m32"])

    data_configure_events(spec_add_fruit)
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")

    CHECK(data_collect_events(timeout_sec=2.), "Fruit related events are received")
    process_stop()


@declare_test("config instrumentation")
def test_external_string():
    """Config external strings PL_EXTERNAL_STRINGS=1"""
    build_target("testprogram", "USE_PL=1 PL_EXTERNAL_STRINGS=1")

    # No decoding
    set_external_strings()
    data_configure_events([EvtSpec("CRASH"), spec_add_fruit])
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")
    events = data_collect_events(timeout_sec=2.)
    CHECK(events, "Fruit related events are received")
    CHECK(events[0].path[-1].count("@")==4, "The strings of the path are obfuscated")
    CHECK(not [1 for e in events if e.path[-1]=="CRASH Stacktrace"], "No crash event has been received")
    # Check CLI is still working
    status, answer = program_cli("test::parameters first=10 second_param=11.5 third=banana")
    CHECK(status==0 and ": 10" in answer and ": 11.5" in answer and ": banana" in answer,
          "CLI is called successfully", status, answer)
    process_stop()

    # With decoding
    set_external_strings(lkup={hash_string("Add fruit"): "Add fruit", hash_string("CRASH"): "CRASH"})
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")
    events = data_collect_events(timeout_sec=2.)
    CHECK(events, "Fruit related events are received")
    CHECK(events[0].path[-1].count("@")==0, "The strings of the path are no more obfuscated")
    CHECK(not [1 for e in events if e.path[-1]=="CRASH Stacktrace"], "No crash event has been received")
    # Check assertions are still working
    status, answer = program_cli("async_assert condvalue=0")
    CHECK(status==0, "CLI to make an assert called successfully", status, answer)
    events = data_collect_events(timeout_sec=2.)
    CHECK([1 for e in events if e.path[-1]=="CRASH"] and not process_is_running(), "Crash event due to the assert has been received")
    process_stop()


@declare_test("config instrumentation")
def test_external_short_string():
    """Config external strings with short hash string PL_EXTERNAL_STRINGS=1 PL_SHORT_STRING_HASH=1"""
    build_target("testprogram", "USE_PL=1 PL_EXTERNAL_STRINGS=1 PL_SHORT_STRING_HASH=1")

    # No decoding
    set_external_strings()
    data_configure_events(spec_add_fruit)
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")
    events = data_collect_events(timeout_sec=2.)
    CHECK(events, "Fruit related events are received")
    CHECK(events[0].path[-1].count("@")==4, "The strings of the path are obfuscated")
    status, answer = program_cli("test::parameters first=10 second_param=11.5 third=banana")
    CHECK(status==0 and ": 10" in answer and ": 11.5" in answer and ": banana" in answer,
          "CLI is called successfully", status, answer)
    process_stop()

    # With decoding
    set_external_strings(lkup={0x5fd6210e: "Add fruit"})
    try:
        launch_testprogram()
        CHECK(True, "Connection established")
    except ConnectionError:
        CHECK(False, "No connection")
    events = data_collect_events(timeout_sec=2.)
    CHECK(events, "Fruit related events are received")
    CHECK(events[0].path[-1].count("@")==0, "The strings of the path are no more obfuscated")
    process_stop()
