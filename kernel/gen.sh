clear
echo 'Building...'
cmake -S . -B build
cmake --build build
echo 'Running...'
./build/kernel