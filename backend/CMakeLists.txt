cmake_minimum_required(VERSION 3.18)
project(HnuTongBackend VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# SQLite3: 优先使用系统 libsqlite3-dev (Linux)，若未安装则自动编译内置 sqlite3.c
# ---------------------------------------------------------------------------
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
  message(STATUS "Using system SQLite3: ${SQLite3_VERSION}")
  set(HNU_SQLITE_TARGET SQLite::SQLite3)
else()
  message(STATUS "Using bundled SQLite3 amalgamation")
  add_library(sqlite3_bundled STATIC third_party/sqlite3/sqlite3.c)
  target_include_directories(sqlite3_bundled PUBLIC third_party/sqlite3)
  target_compile_definitions(sqlite3_bundled PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_OMIT_LOAD_EXTENSION
  )
  set(HNU_SQLITE_TARGET sqlite3_bundled)
endif()

# ---------------------------------------------------------------------------
# 密码学后端: Linux 默认使用 OpenSSL (libcrypto)；Windows MinGW 自动回退原生 CNG (bcrypt)
# ---------------------------------------------------------------------------
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  message(STATUS "Crypto backend: OpenSSL (${OPENSSL_VERSION})")
  set(HNU_CRYPTO_DEFS HNU_USE_OPENSSL=1)
  set(HNU_CRYPTO_LIBS OpenSSL::SSL OpenSSL::Crypto)
elseif(WIN32)
  message(STATUS "Crypto backend: Windows CNG (bcrypt)")
  set(HNU_CRYPTO_DEFS HNU_USE_WINCNG=1)
  set(HNU_CRYPTO_LIBS bcrypt crypt32)
else()
  message(FATAL_ERROR "OpenSSL is required on Linux/POSIX. Install libssl-dev.")
endif()

# ---------------------------------------------------------------------------
# HTTP 客户端传输层: Linux 默认使用 libcurl；Windows MinGW 自动回退原生 WinHTTP
# ---------------------------------------------------------------------------
find_package(CURL QUIET)
if(CURL_FOUND)
  message(STATUS "HTTP client backend: libcurl (${CURL_VERSION_STRING})")
  set(HNU_HTTP_DEFS HNU_USE_LIBCURL=1)
  set(HNU_HTTP_LIBS CURL::libcurl)
elseif(WIN32)
  message(STATUS "HTTP client backend: Windows WinHTTP")
  set(HNU_HTTP_DEFS HNU_USE_WINHTTP=1)
  set(HNU_HTTP_LIBS winhttp ws2_32)
else()
  message(FATAL_ERROR "libcurl is required on Linux/POSIX. Install libcurl4-openssl-dev.")
endif()

# ---------------------------------------------------------------------------
# 核心业务静态库 (便于主服务与单元测试复用)
# ---------------------------------------------------------------------------
set(HNU_CORE_SOURCES
  src/utils/strings_and_time.cpp
  src/utils/logger.cpp
  src/crypto/crypto.cpp
  src/net/http_client.cpp
  src/db/database.cpp
  src/adapters/sandbox_provider.cpp
  src/adapters/qiangzhi_client.cpp
  src/adapters/portal_client.cpp
  src/adapters/notice_crawler.cpp
  src/adapters/chaoxing_client.cpp
  src/adapters/mooc_client.cpp
  src/services/calendar_ics.cpp
  src/services/credit_analyzer.cpp
  src/services/scan_manager.cpp
  src/app/config.cpp
  src/http/api_server.cpp
)

add_library(hnu_core STATIC ${HNU_CORE_SOURCES})

target_include_directories(hnu_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
)

target_compile_definitions(hnu_core PUBLIC
  ${HNU_CRYPTO_DEFS}
  ${HNU_HTTP_DEFS}
  CPPHTTPLIB_THREAD_POOL_COUNT=12
)

if(WIN32)
  target_compile_definitions(hnu_core PUBLIC
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  target_link_libraries(hnu_core PUBLIC ws2_32)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hnu_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

target_link_libraries(hnu_core PUBLIC
  Threads::Threads
  ${HNU_SQLITE_TARGET}
  ${HNU_CRYPTO_LIBS}
  ${HNU_HTTP_LIBS}
)

# ---------------------------------------------------------------------------
# 主服务可执行文件
# ---------------------------------------------------------------------------
add_executable(hnutong_server src/main.cpp)
target_link_libraries(hnutong_server PRIVATE hnu_core)

# ---------------------------------------------------------------------------
# 单元测试可执行文件
# ---------------------------------------------------------------------------
enable_testing()
add_executable(hnutong_tests tests/test_all.cpp)
target_link_libraries(hnutong_tests PRIVATE hnu_core)
add_test(NAME HnuTongCoreTests COMMAND hnutong_tests)
