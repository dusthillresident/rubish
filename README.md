# rubish
*"The ultimate rubbish programming language"*\
A simple programming language with interpreter written in C

The design goal and intention behind Rubish is simplicity, aiming to be extremely simple (both in terms of language syntax and implementation complexity) while still being flexible and powerful. 

A secondary design goal is being embeddable and usable as an extension language for existing applications. The overall design allows for that pretty well I think, but more work is done to realise this goal: I still need to write header files, and probably change the name of structs to avoid naming conflicts

Core features of Rubish:
- Simple syntax, with just 8 reserved characters: `()[];"@#`
- Dynamic typing: a variable can hold a value of any type, and there are 5 main types of value:
  - 'Undefined item' (equivalent to 0 in numerical expressions, or an empty array, etc)
  - Number
  - String
  - Function
  - Array
- Basic numerical calculation functions
- String handling, and basic string manipulation functions
- You can use strings as byte arrays
- Console i/o
- File i/o
- Error/exception trapping and handling
- Arrays, which can have up to 8 dimensions. Single dimensional arrays can be used a bit like 'lists' from contemporary programming languages, and because of the dynamic typing, you can use arrays to construct linked lists
- Functions, to which you can pass parameters by value or variables by reference, and which can take a variable number of arguments, and which can have local variables
