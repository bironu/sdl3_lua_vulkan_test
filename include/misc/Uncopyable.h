#if !defined(UNCOPYABLE_H_)
#define UNCOPYABLE_H_

#define UNCOPYABLE(className)\
	className(const className &) = delete;\
    className(className &&) = delete;\
    const className& operator =(const className &) = delete;\
    const className& operator =(className &&) = delete

#endif // UNCOPYABLE_H_
