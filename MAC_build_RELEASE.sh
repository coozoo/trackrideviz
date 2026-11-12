#!/bin/bash

sw_vers

QT_DIR=$(brew --prefix qt)
QMAKE=$QT_DIR/bin/qmake
MAC_DEPLOY_TOOL=$QT_DIR/bin/macdeployqt

if [ ! -x "$QMAKE" ]; then
    echo "qmake not found at $QMAKE. Make sure you have installed Qt6 with Homebrew."
    exit 1
fi

if [ ! -x "$MAC_DEPLOY_TOOL" ]; then
    echo "macdeployqt not found at $MAC_DEPLOY_TOOL. Make sure you have installed Qt6 with Homebrew."
    exit 1
fi

cd "$(dirname "$0")" || exit 1

appget="$(cat *.pro |grep '^TARGET ='|awk -F\= '{print $2;}'| tr -d ' ')"

APP_NAME=$appget
BUNDLE_NAME=$APP_NAME.app
PROJECT_FILE=$APP_NAME.pro

main(){
    compileProject

    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        echo Failed to CompileProject
        return $RESULT
    fi

    prepareBundle
    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        echo Failed to Prepare bundle
        return $RESULT
    fi

    app_ver=$(cat main.cpp |grep 'const QString APP'|awk -F\" '{print $2;}')
    makeDMG
    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        echo Failed to Make DMG file
        return $RESULT
    fi

    makeZIP
    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        echo Failed to Make ZIP file
        return $RESULT
    fi

    return 0
}

compileProject(){
    echo Compiling...

    $QMAKE $PROJECT_FILE -config release

    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        echo qmake failed, error code $RESULT
        return $RESULT
    fi

    make -j"$(sysctl -n hw.ncpu)"
    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        echo make failed, error code $RESULT
        return $RESULT
    fi

    echo Done.
    return 0
}

prepareBundle(){
    echo Preparing Bundle...
    if [[ ! -d $BUNDLE_NAME ]]
    then
        echo "$BUNDLE_NAME bundle doesn't exist."
        return 1
    fi

    # Translations are embedded in the binary (embed_translations), nothing to copy.
    # Add QtFramework libraries and required plugins into .app bundle. Update dependencies of binary files.
    $MAC_DEPLOY_TOOL $BUNDLE_NAME -verbose=2

    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        echo macdeployqt failed, error code $RESULT
        return $RESULT
    fi

    echo Done.

    otool -L $BUNDLE_NAME/Contents/MacOS/$APP_NAME

    return 0
}

makeDMG(){
    echo Making DMG File...

    if [ ! -d "$BUNDLE_NAME" ]
    then
        echo "$BUNDLE_NAME bundle doesn't exist."
        return 1
    fi

    rm -f "$APP_NAME"_"$app_ver".dmg

    hdiutil create -format UDZO -srcfolder "$BUNDLE_NAME" "$APP_NAME"_"$app_ver".dmg
    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        return $RESULT
    fi

    echo Done.
    return 0
}

makeZIP(){
    echo Making ZIP File...

    if [ ! -d "$BUNDLE_NAME" ]
    then
        echo "$BUNDLE_NAME bundle doesn't exist."
        return 1
    fi

    rm -f "$APP_NAME"_"$app_ver".zip

    zip -r -X "$APP_NAME"_"$app_ver".zip "$BUNDLE_NAME"
    RESULT=$?
    if [ $RESULT -ne 0 ] ; then
        return $RESULT
    fi

    echo Done.
    return 0
}

main
exit $?
