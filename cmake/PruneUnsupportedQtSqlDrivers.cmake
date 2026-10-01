# Qt deploys every SQL driver when QtSql is pulled in by Qt Location/QML.
# SWMMVis uses Qt's embedded SQLite support; it does not expose Mimer,
# ODBC or Qt PostgreSQL connections. Those vendor drivers can contain
# unresolved machine-local client libraries even when macdeployqt exits 0.
# Keep the supported SQLite driver and exclude only these unused drivers.
if(NOT IS_DIRECTORY "${APP_BUNDLE}/Contents")
    message(FATAL_ERROR "APP_BUNDLE must identify a deployed macOS application")
endif()

set(_drivers "${APP_BUNDLE}/Contents/PlugIns/sqldrivers")
if(NOT EXISTS "${_drivers}/libqsqlite.dylib")
    message(FATAL_ERROR "The deployed application is missing its required SQLite driver")
endif()
foreach(_driver IN ITEMS qsqlmimer qsqlodbc qsqlpsql)
    file(REMOVE "${_drivers}/lib${_driver}.dylib")
endforeach()
message(STATUS "Qt SQL deployment: SQLite retained; unused Mimer, ODBC and PostgreSQL drivers excluded")
