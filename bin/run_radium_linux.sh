#!/usr/bin/env bash

set -e

if [ -z "$RADIUM_NSM_EXECUTABLE_NAME" ] ; then
        export RADIUM_NSM_EXECUTABLE_NAME=$(basename -- "$0")
fi

THIS_DIR="$(dirname "$(readlink -f "$0")")"

if [ ! -e "$THIS_DIR/ladspa" ] ; then
    for LADSPA_DIR in \
        /usr/lib64/ladspa \
        /usr/lib/ladspa \
        /usr/local/lib/ladspa \
        "$HOME/.ladspa"
    do
        if [ -d "$LADSPA_DIR" ] ; then
            ln -sfn "$LADSPA_DIR" "$THIS_DIR/ladspa" 2>/dev/null || true
            break
        fi
    done

    if [ ! -e "$THIS_DIR/ladspa" ] ; then
        mkdir -p "$THIS_DIR/ladspa" 2>/dev/null || true
    fi
fi

if ! $THIS_DIR/radium_check_recent_libxcb ; then

    XCB_LIB_DIR="$THIS_DIR"/packages/libxcb-1.13/src/.libs

    if ! file "$XCB_LIB_DIR" ; then
        echo "Unable to find directory $XCB_LIB_DIR"
        exit -1
    fi

    export LD_LIBRARY_PATH="$XCB_LIB_DIR"${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

fi

export LD_LIBRARY_PATH="$THIS_DIR/packages/python27_install/lib:$THIS_DIR/packages/faust/build/lib"${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

# To avoid buggy qt plugins crashing radium (very common).
unset QT_QPA_PLATFORMTHEME
unset QT_QPA_PLATFORM
unset QT_PLUGIN_PATH

if [[ -z "${QT_QPA_PLATFORM_PLUGIN_PATH:-}" ]] ; then
    if command -v qmake-qt5 >/dev/null 2>&1 ; then
        export QT_QPA_PLATFORM_PLUGIN_PATH="$(qmake-qt5 -query QT_INSTALL_PLUGINS)"
    elif command -v qmake >/dev/null 2>&1 ; then
        export QT_QPA_PLATFORM_PLUGIN_PATH="$(qmake -query QT_INSTALL_PLUGINS)"
    fi
fi

if [[ -n "$QT_QPA_PLATFORM_PLUGIN_PATH" ]] ; then
	if QT_QPA_PLATFORM="xcb" LD_LIBRARY_PATH="$LD_LIBRARY_PATH" ldd -r "$QT_QPA_PLATFORM_PLUGIN_PATH"/platforms/libqxcb.so |grep not\ found ; then
		echo
		echo "Can't start. Please install missing dependencies."
		exit -1
	fi


	if QT_QPA_PLATFORM="xcb" LD_LIBRARY_PATH="$LD_LIBRARY_PATH" ldd -r "$QT_QPA_PLATFORM_PLUGIN_PATH"/platforms/libqxcb.so |grep undefined ; then
		echo
		echo "Can't start. Undefined symbols in $QT_QPA_PLATFORM_PLUGIN_PATH/platforms/libqxcb.so. Please contact info@radium.dog and include the following information:"
		QT_QPA_PLATFORM="xcb" LD_LIBRARY_PATH="$LD_LIBRARY_PATH" ldd -r "$QT_QPA_PLATFORM_PLUGIN_PATH"/platforms/libqxcb.so
		exit -1
	fi
fi


QT_QPA_PLATFORM="xcb" LD_LIBRARY_PATH="$LD_LIBRARY_PATH" exec "$THIS_DIR"/radium_linux.bin "$@"
