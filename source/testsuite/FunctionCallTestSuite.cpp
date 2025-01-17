//--------------------------------------------------------------------------------
// (C) Copyright 2014-2015 Stephane Molina, All rights reserved.
// See https://github.com/Dllieu for updates, documentation, and revision history.
//--------------------------------------------------------------------------------
#include <boost/test/unit_test.hpp>
#include <boost/timer/timer.hpp>
#include <map>

#include "tools/Benchmark.h"

// http://www.codeproject.com/Articles/18389/Fast-C-Delegate-Boost-Function-drop-in-replacement
BOOST_AUTO_TEST_SUITE( FunctionCallTestSuite )

#define FUNCTOR_IMPLEMENTATION return 1;

namespace
{
    inline int realImplementation() { FUNCTOR_IMPLEMENTATION; }

    struct ObjectFunctor
    {
        int    operator()() const { FUNCTOR_IMPLEMENTATION; }
    };
}

BOOST_AUTO_TEST_CASE( CallBenchmark )
{
    // std::bind can't be inlined

    // about boost::function
    // - Function object wrappers will be the size of a struct containing a member function pointer and two data pointers.
    //   The actual size can vary significantly depending on the underlying platform; on 32-bit Mac OS X with GCC, this amounts to 16 bytes,
    //   while it is 32 bytes Windows with Visual C++. Additionally, the function object target may be allocated on the heap,
    //   if it cannot be placed into the small-object buffer in the boost::function object.
    // - Copying function object wrappers may require allocating memory for a copy of the function object target.
    //   The default allocator may be replaced with a faster custom allocator or one may choose to allow the function object wrappers to only store function object targets
    //   by reference (using ref) if the cost of this cloning becomes prohibitive. Small function objects can be stored within the boost::function object itself, improving copying efficiency.
    // - With a properly inlining compiler, an invocation of a function object requires one call through a function pointer.
    //   If the call is to a free function pointer, an additional call must be made to that function pointer (unless the compiler has very powerful interprocedural analysis).

    // The cost of boost::function can be reasonably consistently measured at around 20ns +/- 10 ns on a modern >2GHz platform versus directly inlining the code.
    // However, the performance of your application may benefit from or be disadvantaged by boost::function depending on how your C++ optimiser optimises.
    // Similar to a standard function pointer, differences of order of 10% have been noted to the benefit or disadvantage of using boost::function to call a function
    // that contains a tight loop depending on your compilation circumstances.

    // about std::function (short version)
    // - store different types of callable objects. Hence, it must perform some type-erasure magic for the storage. Generally, this implies a dynamic memory allocation (by default through a call to new).
    //   the standard encourages implementations to avoid the dynamic memory allocation for small objects
    // - std::function is not an alternative to templates, but rather a tool for design situations where templates cannot be used
    // - One such use case arises when you need to resolve a call at run-time by invoking a callable object that adheres to a specific signature, but whose concrete type is unknown at compile-time.
    //   This is typically the case when you have a collection of callbacks of potentially different types, but which you need to invoke uniformly;
    //   the type and number of the registered callbacks is determined at run-time based on the state of your program and the application logic. Some of those callbacks could be functors,
    //   some could be plain functions, some could be the result of binding other functions to certain arguments
    //   This result having the underlying call virtual which will very likely prevent inlining
    // - use type-erasure which means it uses indirection to invoke the actual function, Means it first calls a virtual function which then invokes your function. So typically it involves (minimum) two function calls (one of them is virtual)
    // - tl;dr; no inlining / possible dynamic allocation (if not small object) / virtual call
    // - vs function ptr : std::function have a size overhead of 24 bytes (x86-64), the extra size is to allow at least a member function and an object pointer to be stored without requiring heap allocation
    // The implementation of std::function can differ from one implementation to another, but the core idea is that it uses type-erasure.
    // While there are multiple ways of doing it, you can imagine a trivial( not optimal ) solution could be like this ( simplified for the specific case of std::function<int( double )> for the sake of simplicity ) :
    // struct callable_base
    // {
    //     virtual int operator()( double d ) = 0;
    //     virtual ~callable_base() {}
    // };
    // template <typename F>
    // struct callable : callable_base
    // {
    //     F functor;
    //     callable( F functor ) : functor( functor ) {}
    //     virtual int operator()( double d ) { return functor( d ); }
    // };
    // class function_int_double
    // {
    //     std::unique_ptr<callable_base> c;
    // public:
    //     template <typename F>
    //     function( F f )
    //     {
    //         c.reset( new callable<F>( f ) );
    //     }
    //     int operator()( double d ) { return c( d ); }
    //     // ...
    // };
    // In this simple approach the function object would store just a unique_ptr to a base type.For each different functor used with the function, a new type derived from the base is created and an object of that type instantiated dynamically.The std::function object is always of the same size and will allocate space as needed for the different functors in the heap.
    // In real life there are different optimizations that provide performance advantages but would complicate the answer.The type could use small object optimizations, the dynamic dispatch can be replaced by a free - function pointer that takes the f

    // tl;dr : never use bind, always use lambda, or use transparent operator functor
    //       - std::function does have an overhead, always use template for the signature except if no choice

    // About lambda capture
    // Each variable expressly named in the capture list is captured.
    // The default capture will only capture variables that are both not expressly named in the capture list and used in the body of the lambda expression.
    // If a variable is not expressly named and you don't use the variable in the lambda expression, then the variable is not capture

    // When calling a (non-inline) function, the compiler has to place the function parameters / arguments in a location where the called function will expect to find them.
    // In some cases, it will 'push' the arguments onto the process / thread's stack. In other cases, cpu registers might be assigned to specific arguments. Then, the "return address",
    // or the address following the called function is pushed on the stack so that the called function will know how to return control back to the caller.
    // Inlining allows constant-propagation (or even range-propagation) which in turn allow
    // - trimming unused branches/removing inaccessible code
    // - optimizing numeric expressions (taking advantage that i > 0 for example)
    // - realizing that a value was not changed (when passing pointers)
    // Which is why de-virtualization is so sought after. The overhead of a virtual function call compared to a regular function call is 'negligible' for any non-trivial function; however run-time dispatch prevents inlining
    
    // Inline only along hot paths. Excessive inlining bloats executables. Can decrease I-Cache, TLB, and paging effectiveness.
    auto call_n = [] ( auto& f, auto n ) { auto res = 0; for ( auto i = 0; i < n; ++i ) res += f(); return res; };
    auto test = [ &call_n ] ( auto n )
    {
        double bindT, directT, functorT, lambdaT;
        std::tie( bindT, directT, functorT, lambdaT ) = tools::benchmark( n,
                                                                          [ &, f = std::bind( &realImplementation ) ] { return call_n( f, n ); }, // no inlining
                                                                          [ &, f = realImplementation ] { return call_n( f, n ); }, // no inlining
                                                                          [ &, f = ObjectFunctor() ] { return call_n( f, n ); },
                                                                          [ &, f = [] { FUNCTOR_IMPLEMENTATION; } ]{ return call_n( f, n ); } );

        BOOST_CHECK( bindT > functorT && directT > functorT );
        BOOST_CHECK( bindT > lambdaT && directT > lambdaT );
    };

    tools::run_test< int >( "bind;direct;functor;lambda;", test, 10'000, 100'000 );
}

#undef FUNCTOR_IMPLEMENTATION

BOOST_AUTO_TEST_CASE( LambdaDetailsTest )
{
    int captured = 42;

    // generate something close to:
    // struct l0_t {
    // int& captured;
    // l0_t(int& _captured) : captured(_captured) {}
    // void operator()(int x) const { captured += x; }
    // } l0(captured);
    auto l0 = [&captured]( int x ){ captured += x; };
    auto l1 = [&captured]( int x ){ captured -= x; };
    auto l2 = [&captured]( int x ){ captured = x + 1; };

    // The type of the lambda-expression (which is also the type of the closure object) is a unique, unnamed non union class type — called the closure type
    // Each lambda has a different type : l0, l1 and l2 have no common type., so either use boost::variant (but need to explicitly tell the type when getting the lambda) or std::function (same signature so it's accepted)
    // variant cannot be "empty", and for such a lambda-variant therefore it is not default-constructible. For std::function you will be able to default-construct it
    std::vector< std::function< void ( int ) >/*boost::variant< decltype( l0 ), decltype( l1 ), decltype( l2 ) >*/ > fs;
    fs.emplace_back( std::move( l0 ) );
    fs.emplace_back( std::move( l1 ) );
    fs.emplace_back( std::move( l2 ) );

    // auto f = boost::get< decltype( l2 ) >( fs[ 2 ] );
    for ( auto& f : fs )
        f( 0 );

    BOOST_CHECK( captured == 1 );

    // if by copy, captured is const by default in the functor
    auto lm0 = [captured] () mutable { ++captured; return captured; };
    BOOST_CHECK( lm0() == 2 );
    BOOST_CHECK( lm0() == 3 );

    auto lm1 = lm0;
    BOOST_CHECK( lm1() == 4 );
}

BOOST_AUTO_TEST_SUITE_END() // FunctionCallTestSuite
