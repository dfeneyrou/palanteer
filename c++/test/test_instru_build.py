#! /usr/bin/env python3

# System import
import time

# Local import
from testframework import *  # Decorators, LOG, CHECK, KPI
from test_base import *  # Test common helpers


# Base case
@declare_test("build instrumentation")
def test_build_instru1():
    """USE_PL=1"""
    build_target("testprogram", test_build_instru1.__doc__)


# Base case
@declare_test("build instrumentation")
def test_build_instru2():
    """USE_PL=0"""
    build_target("testprogram", test_build_instru2.__doc__)


# No operation cases (check)
@declare_test("build instrumentation")
def test_build_instru3():
    """USE_PL=0 PL_NOCONTROL=0"""
    build_target("testprogram", test_build_instru3.__doc__)


# No operation cases (check)
@declare_test("build instrumentation")
def test_build_instru4():
    """USE_PL=0 PL_NOEVENT=0"""
    build_target("testprogram", test_build_instru4.__doc__)


# No operation cases (check)
@declare_test("build instrumentation")
def test_build_instru5():
    """USE_PL=0 PL_NOASSERT=0"""
    build_target("testprogram", test_build_instru5.__doc__)


# Enabled except one feature
@declare_test("build instrumentation")
def test_build_instru6():
    """USE_PL=1 PL_NOCONTROL=1"""
    build_target("testprogram", test_build_instru6.__doc__)


# Enabled except one feature
@declare_test("build instrumentation")
def test_build_instru7():
    """USE_PL=1 PL_NOEVENT=1"""
    build_target("testprogram", test_build_instru7.__doc__)


# Enabled except one feature
@declare_test("build instrumentation")
def test_build_instru8():
    """USE_PL=1 PL_NOASSERT=1"""
    build_target("testprogram", test_build_instru8.__doc__)


# Enabled except two features (one remaining)
@declare_test("build instrumentation")
def test_build_instru9():
    """USE_PL=1 PL_NOEVENT=1 PL_NOASSERT=1"""
    build_target("testprogram", test_build_instru9.__doc__)


# Enabled except two features (one remaining)
@declare_test("build instrumentation")
def test_build_instru10():
    """USE_PL=1 PL_NOCONTROL=1 PL_NOASSERT=1"""
    build_target("testprogram", test_build_instru10.__doc__)


# Enabled except two features (one remaining)
@declare_test("build instrumentation")
def test_build_instru11():
    """USE_PL=1 PL_NOCONTROL=1 PL_NOEVENT=1"""
    build_target("testprogram", test_build_instru11.__doc__)


# Enabled but all features are disabled
@declare_test("build instrumentation")
def test_build_instru12():
    """USE_PL=1 PL_NOCONTROL=1 PL_NOEVENT=1 PL_NOASSERT=1"""
    build_target("testprogram", test_build_instru12.__doc__)


# Simple assertions
@declare_test("build instrumentation")
def test_build_instru13():
    """USE_PL=1 PL_SIMPLE_ASSERT=1"""
    build_target("testprogram", test_build_instru13.__doc__)


# Simple assertions but assertions disabled
@declare_test("build instrumentation")
def test_build_instru14():
    """USE_PL=1 PL_SIMPLE_ASSERT=1 PL_NOASSERT=1"""
    build_target("testprogram", test_build_instru14.__doc__)


# String features
@declare_test("build instrumentation")
def test_build_instru15():
    """USE_PL=1 PL_EXTERNAL_STRINGS=1"""
    build_target("testprogram", test_build_instru15.__doc__)


# String features
@declare_test("build instrumentation")
def test_build_instru16():
    """USE_PL=1 PL_SHORT_STRING_HASH=1"""
    build_target("testprogram", test_build_instru16.__doc__)


# String features
@declare_test("build instrumentation")
def test_build_instru17():
    """USE_PL=1 PL_EXTERNAL_STRINGS=1 PL_SHORT_STRING_HASH=1"""
    build_target("testprogram", test_build_instru17.__doc__)


# Other features
@declare_test("build instrumentation")
def test_build_instru18():
    """USE_PL=1 PL_CONTEXT_SWITCH=0"""
    build_target("testprogram", test_build_instru18.__doc__)


# Other features
@declare_test("build instrumentation")
def test_build_instru19():
    """USE_PL=1 PL_OVERLOAD_NEW_DELETE=0"""
    build_target("testprogram", test_build_instru19.__doc__)


# Other features
@declare_test("build instrumentation")
def test_build_instru20():
    """USE_PL=1 PL_CATCH_SIGNALS=0"""
    build_target("testprogram", test_build_instru20.__doc__)


# Other features
@declare_test("build instrumentation")
def test_build_instru21():
    """USE_PL=1 PL_IMPL_STACKTRACE=0"""
    build_target("testprogram", test_build_instru21.__doc__)


# Other features with deactivated Palanter
@declare_test("build instrumentation")
def test_build_instru22():
    """USE_PL=0 PL_CONTEXT_SWITCH=0"""
    build_target("testprogram", test_build_instru22.__doc__)


# Other features with deactivated Palanter
@declare_test("build instrumentation")
def test_build_instru23():
    """USE_PL=0 PL_OVERLOAD_NEW_DELETE=0"""
    build_target("testprogram", test_build_instru23.__doc__)


# Other features with deactivated Palanter
@declare_test("build instrumentation")
def test_build_instru24():
    """USE_PL=0 PL_CATCH_SIGNALS=0"""
    build_target("testprogram", test_build_instru24.__doc__)


# Other features with deactivated Palanter
@declare_test("build instrumentation")
def test_build_instru25():
    """USE_PL=0 PL_IMPL_STACKTRACE=0"""
    build_target("testprogram", test_build_instru25.__doc__)


# Virtual threads feature
@declare_test("build instrumentation")
def test_build_instru26():
    """USE_PL=1 PL_VIRTUAL_THREADS=1"""
    build_target("testprogram", test_build_instru26.__doc__)


# Virtual threads feature
@declare_test("build instrumentation")
def test_build_instru27():
    """USE_PL=1 PL_NOEVENT=0 PL_VIRTUAL_THREADS=1"""
    build_target("testprogram", test_build_instru27.__doc__)


# Virtual threads feature
@declare_test("build instrumentation")
def test_build_instru28():
    """USE_PL=0 PL_VIRTUAL_THREADS=1"""
    build_target("testprogram", test_build_instru28.__doc__)
