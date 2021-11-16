#! /usr/bin/env python3

# System import
import os
import subprocess
import shutil
import glob
import multiprocessing  # For the CPU qty

# Local import
import palanteer_scripting
from testframework import *  # Decorators, LOG, CHECK, KPI

work_dir = None
work_dirname = "tmpTesting"
program_path = "./bin/testprogram"


# Register the test global preparation
@prepare_suite("")
def prepare_build():
    global work_dir, work_dirname

    # Check explicitely that required tools are present
    assert sys.version_info >= (
        3,
        7,
    ), "Python 3.7 is required at least (usage of subprocess module with 'capture_output' parameter...)"
    if sys.platform == "win32":
        required_tools = [["cmake", "/?"], ["nmake", "/?"], ["cl.exe"]]
    else:
        required_tools = [["cmake", "--version"], ["g++", "--version"]]
    for t in required_tools:
        try:
            subprocess.run(
                t,
                universal_newlines=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except FileNotFoundError:
            print(
                "ERROR: '%s' is not found. This tool is required to pass the tests."
                % t[0],
                file=sys.stderr,
            )
            sys.exit(1)

    # Create the working directory and make it the current directory
    work_dir = os.path.join(os.getcwd(), work_dirname)
    if os.path.exists(work_dir):
        shutil.rmtree(work_dir)
    os.mkdir(work_dir)
    assert os.path.isdir(work_dir)
    os.chdir(work_dir)


# Register the test global cleaning
@clean_suite("")
def clean_build():
    global work_dir
    # Clean the working directory
    palanteer_scripting.process_stop()
    os.chdir("..")
    shutil.rmtree(work_dirname)


# Test helpers
# ============


def run_cmd(cmd_and_args_list, capture_output=True):
    if force_no_capture_output():
        capture_output = False

    res = subprocess.run(
        cmd_and_args_list, universal_newlines=True, capture_output=capture_output
    )  # capture_output => Python 3.7 minimum
    try:
        res.check_returncode()
    except subprocess.CalledProcessError as e:
        print("Error when calling %s" % " ".join(cmd_and_args_list), file=sys.stderr)
        print("STDERR:\n%s" % res.stderr)
        print("STDOUT:\n%s" % res.stdout)
        raise
    return res


def build_target(target_name, string_flags, compilation_flags=[]):
    LOG(
        "Building '%s' with flags %s %s"
        % (target_name, string_flags, " ".join(compilation_flags))
    )

    # Ensure previous process is stopped
    palanteer_scripting.process_stop()

    build_type = "Debug"
    rootProject = os.path.join("..", "..", "..")
    cmake_flags = [
        "-DCUSTOM_FLAGS=%s"
        % " ".join(["-D%s" % f for f in string_flags.split()] + compilation_flags)
    ]
    cmake_flags.append("-DCMAKE_BUILD_TYPE=%s" % build_type)
    if sys.platform == "win32":
        cmake_flags.extend(["-G", "NMake Makefiles"])
    try:
        run_cmd(["cmake", rootProject] + cmake_flags)
    except subprocess.CalledProcessError as e:
        CHECK(False, "ERROR while configuring with cmake:", e.stderr)
        return False

    # Compile
    try:
        # Build
        if sys.platform == "win32":
            cmdArgs = ["nmake", target_name]
        else:
            cmdArgs = ["make", target_name, "-j", "%d" % multiprocessing.cpu_count()]
        startDate = time.time()
        run_cmd(cmdArgs)
        endDate = time.time()
        # Display
        if sys.platform == "win32":
            progPath = (
                glob.glob("bin/%s.exe" % target_name)
                + glob.glob("bin/%s.dll" % target_name[3:])
            )[0]
        else:
            progPath = (
                glob.glob("bin/%s" % target_name) + glob.glob("lib/%s.so" % target_name)
            )[0]
        progSize = os.stat(progPath).st_size
        CHECK(True, "built in %.2f s, size %6d B" % (endDate - startDate, progSize))
    except subprocess.CalledProcessError as e:
        CHECK(
            False,
            "build error:",
            "\n".join("   %s" % l for l in e.stderr.split("\n")[:15]),
        )
        return False

    return True


def launch_testprogram(
    command="collect",
    duration=10,
    threadgroup_qty=1,
    pass_first_freeze_point=False,
    connection_timeout_sec=5.0,
    capture_output=True,
):
    # Ensure previous process is stopped
    palanteer_scripting.process_stop()
    # Launch testProgram with the expected parameters
    palanteer_scripting.process_launch(
        program_path,
        [
            command,
            "--port",
            str(SERVER_PORT),
            "-l",
            str(duration),
            "-t",
            str(threadgroup_qty),
        ],
        pass_first_freeze_point=pass_first_freeze_point,
        connection_timeout_sec=connection_timeout_sec,
        capture_output=capture_output,
    )
