# cl cxx flags
set(CMAKE_CXX_FLAGS_DEBUG_INIT   "/MDd /Ob0 /Od /D_DEBUG /Zi /RTC1")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "/MD  /Ob2 /O2 /D NDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "/MD  /Ob1 /O2 /D NDEBUG /Zi")

# cl c flags
set(CMAKE_C_FLAGS_DEBUG_INIT   "/MDd /Ob0 /Od /D_DEBUG /Zi /RTC1")
set(CMAKE_C_FLAGS_RELEASE_INIT "/MD  /Ob2 /O2 /D NDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "/MD  /Ob1 /O2 /D NDEBUG /Zi")

