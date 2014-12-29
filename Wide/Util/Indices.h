namespace Wide {
    namespace Util {
        template<unsigned...> struct indices{};

        template<unsigned N, unsigned... Is>
        struct indices_gen : indices_gen < N - 1, N - 1, Is... > {};

        template<unsigned... Is>
        struct indices_gen<0, Is...> : indices < Is... > {};
    }
}