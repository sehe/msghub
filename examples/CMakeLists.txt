#LINK_LIBRARIES(msghub)

FIND_PACKAGE(Boost 1.65.0 COMPONENTS filesystem)
LINK_LIBRARIES(${Boost_FILESYSTEM_LIBRARY})
ADD_EXECUTABLE(server server.cpp)
TARGET_LINK_LIBRARIES(server msghub)

ADD_EXECUTABLE(client client.cpp)
TARGET_LINK_LIBRARIES(client msghub)
