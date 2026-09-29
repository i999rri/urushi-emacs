#pragma once

// What a test measured, said beside what the run before it measured.
//
// A number on its own says little: what says something is whether it
// moved. So each run writes what it measured beside the test program
// and prints the one before it, and two runs can be put side by side
// without anyone writing the numbers down.

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace urushi::tests
{
    // Whether the standard library is the checked one, which takes a
    // piece of the heap for every container it makes and is slower by
    // a good deal: what a measurement comes to there is not what it
    // comes to in what is shipped, so measurements are only made and
    // held to where this is false.
    constexpr bool kChecked =
#if defined _ITERATOR_DEBUG_LEVEL && _ITERATOR_DEBUG_LEVEL > 0
        true;
#else
        false;
#endif

    namespace measured
    {
        // Beside the test program, so it follows the build it belongs
        // to rather than the tree.
        inline char const* const kBefore = "measured.last";

        inline std::map<std::string, double>& Before()
        {
            static std::map<std::string, double> before = [] {
                std::map<std::string, double> read;
                std::ifstream file{ kBefore };
                std::string name;
                double was{};

                while (file >> name >> was)
                {
                    read[name] = was;
                }
                return read;
            }();
            return before;
        }

        inline std::map<std::string, double>& Now()
        {
            static std::map<std::string, double> now;
            return now;
        }
    }

    // Say VALUE, in UNIT, under NAME, beside what it was last run.
    inline void Measured(std::string const& name, double value,
                         char const* unit)
    {
        measured::Now()[name] = value;

        std::ostringstream said;
        said << name << ": " << value << " " << unit;

        if (auto was = measured::Before().find(name);
            was != measured::Before().end())
        {
            double const then = was->second;

            said << " (was " << then;
            if (then > 0)
            {
                said << ", " << (value - then) / then * 100 << "%";
            }
            said << ")";
        }
        else
        {
            said << " (nothing to compare with yet)";
        }
        std::cout << said.str() << "\n";

        std::ofstream file{ measured::kBefore };
        for (auto const& [kept, number] : measured::Now())
        {
            file << kept << " " << number << "\n";
        }
    }
}
