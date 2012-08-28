set(T2H_VERSION_EX 00000100)

math(EXPR T2H_VERSION_MAJOR "${T2H_VERSION_EX} / 1000000")
math(EXPR T2H_VERSION_MINOR "${T2H_VERSION_EX} / 10000 % 100")
math(EXPR T2H_VERSION_PATCH "${T2H_VERSION_EX} / 100 % 100")
math(EXPR T2H_VERSION_BUILD "${T2H_VERSION_EX} % 100")

set(
	T2H_VERSION 
	${T2H_VERSION_MAJOR}.${T2H_VERSION_MINOR}.${T2H_VERSION_PATCH}.${T2H_VERSION_BUILD}
)

