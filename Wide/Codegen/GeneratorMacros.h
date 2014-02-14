#define CreateLifetimeEnd(...) CreateLifetimeEndFileLine(__VA_ARGS__, __FILE__, __LINE__)
#define CreateVariable(...) CreateVariableFileLine(__VA_ARGS__, __FILE__, __LINE__)
#define CreateFunctionCall(...) CreateFunctionCallFileLine(__FILE__, __LINE__, __VA_ARGS__)
#define CreateStore(...) CreateStoreFileLine(__VA_ARGS__, __FILE__, __LINE__)