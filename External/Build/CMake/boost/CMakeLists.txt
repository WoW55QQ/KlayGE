SET(BOOST_MAJOR_VERSION 1)
SET(BOOST_MINOR_VERSION 72)
SET(BOOST_SUBMINOR_VERSION 0)
SET(BOOST_VERSION ${BOOST_MAJOR_VERSION}.${BOOST_MINOR_VERSION}.${BOOST_SUBMINOR_VERSION})

SET(BOOST_PROJECT_DIR "${EXTERNAL_PROJECT_DIR}/boost")
SET(BOOST_OUTPUT_DIR "${EXTERNAL_PROJECT_DIR}/lib/boost/${KLAYGE_PLATFORM_NAME}")
SET(REL_PATH "External/Downloads/boost_${BOOST_MAJOR_VERSION}_${BOOST_MINOR_VERSION}_${BOOST_SUBMINOR_VERSION}.patched.7z")
SET(PACKAGE_FILE_ID "F013F55F2CFDCADDB168F717B2DE8763E6D0D958")

DOWNLOAD_PACKAGE("boost" ${REL_PATH} ${PACKAGE_FILE_ID} "${BOOST_PROJECT_DIR}/LICENSE_1_0.txt")

set(Boost_INCLUDE_DIR "${EXTERNAL_PROJECT_DIR}/boost" CACHE INTERNAL "" FORCE)
