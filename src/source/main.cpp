/**
 * @file main.cpp
 * @brief File that contains the application entry point which tests the dynamic buffer.
 *
 * @author Rakesh Kumar
 */

#include <Windows.h>
#include <iostream>
#include "../header/DynamicBuffer.h"
#include <chrono>

/**
* @brief The entry point of the application.
* 
* @return An exit code sent to the operating system.
*/
int main() {
    // NEW TASKS
    DynBuffer<unsigned long long>* dynBuffer = new DynBuffer<unsigned long long>();
    BufferSegmentOwner* owner = new BufferSegmentOwner(BUFFER_SEGMENT_ACCESS_LEVEL::WRITE);


    dynBuffer->use<void, unsigned long long, double>(
        owner,
        std::function<void(unsigned long long, double)>([](unsigned long long a, double b) {
            // Do something inside
            debug("This is a debug message" + std::to_string((a + b)));
            }),
        42,
        3.14
    );


    // Start clock
    auto writerTStart = std::chrono::steady_clock::now();
    // WRITE TESTS
    // ONE WAY WRITES (WITHOUT ANY READ)
    for (unsigned long long i = 1; i <= 10035; i++) {
        dynBuffer->write(i, owner);
    }
    auto writerTEnd = std::chrono::steady_clock::now();

    // auto readerTStart = std::chrono::steady_clock::now();
    // // READ TESTS
    // // ONE WAY READS
    // for (unsigned long long i = 1; i <= 10035; i++) {
    //     try {
    //         std::cout << dynBuffer->read(owner) << std::endl;
    //     }
    //     catch (const std::exception& e) {
    //         std::cout << e.what() << std::endl;
    //     }
    // }
    // auto readerTEnd = std::chrono::steady_clock::now();


    dynBuffer->use<void, unsigned long long, double>(
        owner,
        std::function<void(unsigned long long, double)>([](unsigned long long a, double b) {
            // Do something inside
            debug("This is a debug message" + std::to_string((a * b)));
            }),
        42,
        3.14
    );    

    std::pair<BufferSegmentOwner*, BufferSegmentOwner*> readerWriterPair = BufferSegmentOwner::getReaderWriterPair("reader", "writer");

    std::cout << "Individual Write Time = " << std::chrono::duration<double, std::milli>(writerTEnd - writerTStart).count() << " ms" << std::endl;
    // std::cout << "Individual Read Time = " << std::chrono::duration<double, std::milli>(readerTEnd - readerTStart).count() << " ms" << std::endl;

    // Clear up before exit
    delete dynBuffer;
    dynBuffer = nullptr;

    return 0;
}