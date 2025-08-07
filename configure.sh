sudo apt-get install build-essential g++ libsqlite3-dev libboost-all-dev libssl-dev
it submodule update --init --recursive --depth 1

# build gcc:
# sudo apt install libmpfr-dev libgmp3-dev libmpc-dev -y
# wget https://ftp.gnu.org/gnu/gcc/gcc-15.1.0/gcc-15.1.0.tar.gz
# tar -xf gcc-15.1.0.tar.gz
# cd gcc-15.1.0
# ./configure -v --build=x86_64-linux-gnu --host=x86_64-linux-gnu --target=x86_64-linux-gnu --enable-checking=release --enable-languages=c,c++ --disable-multilib
# make -j2
# sudo make install

# configure boost
cd dependencies/boost
#git submodule update --init tools/build tools/boost_install \
#    libs/config libs/core libs/system libs/throw_exception \
#    libs/assert libs/static_assert libs/headers libs/charconv \
#    libs/compat \
#    libs/asio libs/mysql libs/crc libs/serialization

./bootstrap.sh
./b2 --with-serialization
