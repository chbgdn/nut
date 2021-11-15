dnl Check for LIBUSB 1.0 or 0.1 compiler flags. On success, set
dnl nut_have_libusb="yes" and set LIBUSB_CFLAGS and LIBUSB_LIBS. On failure, set
dnl nut_have_libusb="no". This macro can be run multiple times, but will
dnl do the checking only once.

AC_DEFUN([NUT_CHECK_LIBUSB],
[
if test -z "${nut_have_libusb_seen}"; then
	nut_have_libusb_seen=yes
	NUT_CHECK_PKGCONFIG

	dnl save CFLAGS and LIBS
	CFLAGS_ORIG="${CFLAGS}"
	LIBS_ORIG="${LIBS}"
	nut_usb_lib=""

	dnl TOTHINK: What if there are more than 0.1 and 1.0 to juggle?
	AS_IF([test x"$have_PKG_CONFIG" = xyes],
		[AC_MSG_CHECKING(for libusb-1.0 version via pkg-config)
		 LIBUSB_1_0_VERSION="`$PKG_CONFIG --silence-errors --modversion libusb-1.0 2>/dev/null`" \
		    && test -n "${LIBUSB_1_0_VERSION}" \
		    || LIBUSB_1_0_VERSION="none"
		 AC_MSG_RESULT(${LIBUSB_1_0_VERSION} found)

		 AC_MSG_CHECKING(for libusb(-0.1) version via pkg-config)
		 LIBUSB_0_1_VERSION="`pkg-config --silence-errors --modversion libusb 2>/dev/null`" \
		    && test -n "${LIBUSB_0_1_VERSION}" \
		    || LIBUSB_0_1_VERSION="none"
		 AC_MSG_RESULT(${LIBUSB_0_1_VERSION} found)
		],
		[LIBUSB_0_1_VERSION="none"
		 LIBUSB_1_0_VERSION="none"
		 AC_MSG_NOTICE([can not check libusb settings via pkg-config])
		]
	)

	dnl Note: it seems the script was only shipped for libusb-0.1
	AC_MSG_CHECKING([via libusb-config (if present)])
	LIBUSB_CONFIG_VERSION="`libusb-config --version 2>/dev/null`" \
	    && test -n "${LIBUSB_CONFIG_VERSION}" \
	    || LIBUSB_CONFIG_VERSION="none"
	AC_MSG_RESULT(${LIBUSB_CONFIG_VERSION} found)

	dnl By default, prefer newest available, and if anything is known
	dnl to pkg-config, prefer that. Otherwise, fall back to script data:
	AS_IF([test x"${LIBUSB_1_0_VERSION}" != xnone],
		[LIBUSB_VERSION="${LIBUSB_1_0_VERSION}"
		 nut_usb_lib="(libusb-1.0)"
		],
		[AS_IF([test x"${LIBUSB_0_1_VERSION}" != xnone],
			[LIBUSB_VERSION="${LIBUSB_0_1_VERSION}"
			 nut_usb_lib="(libusb-0.1)"
			],
			[LIBUSB_VERSION="${LIBUSB_CONFIG_VERSION}"
			 dnl TODO: This assumes 0.1; check for 1.0+ somehow?
			 nut_usb_lib="(libusb-0.1-config)"
			]
		)]
	)

	AC_MSG_CHECKING(for libusb preferred version)
	AC_ARG_WITH(libusb-version,
		AS_HELP_STRING([@<:@--with-libusb-version=(auto|0.1|1.0)@:>@], [require build with specified version of libusb library]),
	[
		case "${withval}" in
		auto) ;; dnl Use preference picked above
		1.0) dnl NOTE: Assuming there is no libusb-config-1.0 or similar script, never saw one
			if test x"${LIBUSB_1_0_VERSION}" = xnone; then
				AC_MSG_ERROR([option --with-libusb-version=${withval} was required, but this library version was not detected])
			fi
			LIBUSB_VERSION="${LIBUSB_1_0_VERSION}"
			nut_usb_lib="(libusb-1.0)"
			;;
		0.1)
			if test x"${LIBUSB_0_1_VERSION}" = xnone \
			&& test x"${LIBUSB_CONFIG_VERSION}" = xnone \
			; then
				AC_MSG_ERROR([option --with-libusb-version=${withval} was required, but this library version was not detected])
			fi
			if test x"${LIBUSB_0_1_VERSION}" != xnone ; then
				LIBUSB_VERSION="${LIBUSB_0_1_VERSION}"
				nut_usb_lib="(libusb-0.1)"
			else
				LIBUSB_VERSION="${LIBUSB_CONFIG_VERSION}"
				nut_usb_lib="(libusb-0.1-config)"
			fi
			;;
		yes|no|*)
			AC_MSG_ERROR([invalid option value --with-libusb-version=${withval} - see docs/configure.txt])
			;;
		esac
	], [])
	AC_MSG_RESULT([${LIBUSB_VERSION} ${nut_usb_lib}])

	AS_IF([test x"${LIBUSB_1_0_VERSION}" != xnone && test x"${nut_usb_lib}" != x"(libusb-1.0)" ],
		[AC_MSG_NOTICE([libusb-1.0 support was detected, but another was chosen ${nut_usb_lib}])]
	)

	AS_CASE([${nut_usb_lib}],
		["(libusb-1.0)"], [
			CFLAGS="`pkg-config --silence-errors --cflags libusb-1.0 2>/dev/null`"
			LIBS="`pkg-config --silence-errors --libs libusb-1.0 2>/dev/null`"
			AC_DEFINE(WITH_LIBUSB_1_0, 1, [Define to 1 for version 1.0 of the libusb (via pkg-config).])
			],
		["(libusb-0.1)"], [
			CFLAGS="`$PKG_CONFIG --silence-errors --cflags libusb 2>/dev/null`"
			LIBS="`$PKG_CONFIG --silence-errors --libs libusb 2>/dev/null`"
			AC_DEFINE(WITH_LIBUSB_0_1, 1, [Define to 1 for version 0.1 of the libusb (via pkg-config).])
			],
		["(libusb-0.1-config)"], [
			CFLAGS="`libusb-config --cflags 2>/dev/null`"
			LIBS="`libusb-config --libs 2>/dev/null`"
			AC_DEFINE(HAVE_LIBUSB_0_1, 1, [Define to 1 for version 0.1 of the libusb (via libusb-config).])
			],
		[dnl default, for other versions or "none"
			AC_MSG_WARN([Defaulting libusb configuration])
			LIBUSB_VERSION="none"
			CFLAGS=""
			LIBS="-lusb"
		]
	)

	AC_MSG_CHECKING(for libusb cflags)
	AC_ARG_WITH(usb-includes,
		AS_HELP_STRING([@<:@--with-usb-includes=CFLAGS@:>@], [include flags for the libusb library]),
	[
		case "${withval}" in
		yes|no)
			AC_MSG_ERROR(invalid option --with(out)-usb-includes - see docs/configure.txt)
			;;
		*)
			CFLAGS="${withval}"
			;;
		esac
	], [])
	AC_MSG_RESULT([${CFLAGS}])

	AC_MSG_CHECKING(for libusb ldflags)
	AC_ARG_WITH(usb-libs,
		AS_HELP_STRING([@<:@--with-usb-libs=LIBS@:>@], [linker flags for the libusb library]),
	[
		case "${withval}" in
		yes|no)
			AC_MSG_ERROR(invalid option --with(out)-usb-libs - see docs/configure.txt)
			;;
		*)
			LIBS="${withval}"
			;;
		esac
	], [])
	AC_MSG_RESULT([${LIBS}])

	dnl check if libusb is usable
	if test -n "${LIBUSB_VERSION}"; then
		dnl Test specifically for libusb-1.0 via pkg-config, else fall back below
		test -n "$PKG_CONFIG" \
			&& test x"${nut_usb_lib}" = x"(libusb-1.0)" \
			&& $PKG_CONFIG --silence-errors --atleast-version=1.0 libusb-1.0 2>/dev/null
		if test "$?" = "0"; then
			dnl libusb 1.0: libusb_set_auto_detach_kernel_driver
			AC_CHECK_HEADERS(libusb.h, [nut_have_libusb=yes], [nut_have_libusb=no], [AC_INCLUDES_DEFAULT])
			AC_CHECK_FUNCS(libusb_init, [], [nut_have_libusb=no])
			dnl Check for libusb "force driver unbind" availability
			AC_CHECK_FUNCS(libusb_set_auto_detach_kernel_driver)
			dnl libusb 1.0: libusb_detach_kernel_driver
			dnl FreeBSD 10.1-10.3 have this, but not libusb_set_auto_detach_kernel_driver
			AC_CHECK_FUNCS(libusb_detach_kernel_driver)
		else
			dnl libusb 0.1, or missing pkg-config :
			AC_CHECK_HEADERS(usb.h, [nut_have_libusb=yes], [nut_have_libusb=no], [AC_INCLUDES_DEFAULT])
			AC_CHECK_FUNCS(usb_init, [], [nut_have_libusb=no])
			dnl Check for libusb "force driver unbind" availability
			AC_CHECK_FUNCS(usb_detach_kernel_driver_np)
		fi
	else
		nut_have_libusb=no
	fi

	if test "${nut_have_libusb}" = "yes"; then
		dnl ----------------------------------------------------------------------
		dnl additional USB-related checks

		dnl Solaris 10/11 USB handling (need librt and libusb runtime path)
		dnl Should we check for `uname -o == illumos` to avoid legacy here?
		dnl Or better yet, perform some active capability tests for need of
		dnl workarounds or not? e.g. OpenIndiana should include a capable
		dnl version of libusb-1.0.23+ tailored with NUT tests in mind...
		dnl HPUX, since v11, needs an explicit activation of pthreads
		case "${target_os}" in
			solaris2.1* )
				AC_MSG_CHECKING([for Solaris 10 / 11 specific configuration for usb drivers])
				AC_SEARCH_LIBS(nanosleep, rt)
				LIBS="-R/usr/sfw/lib ${LIBS}"
				dnl FIXME: Sun's libusb doesn't support timeout (so blocks notification)
				dnl and need to call libusb close upon reconnection
				AC_DEFINE(SUN_LIBUSB, 1, [Define to 1 for Sun version of the libusb.])
				SUN_LIBUSB=1
				AC_MSG_RESULT([${LIBS}])
				;;
			hpux11*)
				CFLAGS="${CFLAGS} -lpthread"
				;;
		esac
	fi

	if test "${nut_have_libusb}" = "yes"; then
		LIBUSB_CFLAGS="${CFLAGS}"
		LIBUSB_LIBS="${LIBS}"
	fi

	dnl restore original CFLAGS and LIBS
	CFLAGS="${CFLAGS_ORIG}"
	LIBS="${LIBS_ORIG}"
fi
])
