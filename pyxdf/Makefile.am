# internal python module for xdf object (named pyxdf)
_pyxdf_la_SOURCES = pyxdf.c
_pyxdf_la_CPPFLAGS = $(PYTHONDEV_CPPFLAGS) -Wno-cast-function-type -I$(top_srcdir)/src
_pyxdf_la_CFLAGS = $(PYTHONDEV_CFLAGS)
_pyxdf_la_LDFLAGS = \
       -avoid-version \
       -no-undefined \
       -module \
       $(PYTHONDEV_LDFLAGS) \
       $(NULL)
if OS_TYPE_WIN32
# https://docs.python.org/fr/3/faq/windows.html#is-a-pyd-file-the-same-as-a-dll
_pyxdf_la_LDFLAGS += -shrext ".pyd"
endif
_pyxdf_la_LIBADD = $(PYTHONDEV_LIBS) ../src/libxdffileio.la
pyexecdir=$(pythondir)/xdf
pyexec_LTLIBRARIES = _pyxdf.la

# public xdf module
xdfdir = $(pythondir)/xdf
xdf_PYTHON = __init__.py

# test targets
TESTS_ENVIRONMENT = builddir=$(builddir) srcdir=$(srcdir) builddirlibs=$(builddir)/.libs srcdirlibs=../src/.libs/
noinst_PYTHON = tests/pytests.py
TESTS = tests/python-unittests.tap
dist_check_SCRIPTS = tests/python-unittests.tap
TEST_EXTENSIONS = .test .tap
TAP_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/build-aux/tap-driver.sh
