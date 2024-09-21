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
- Functions, to which you can pass parameters by value or variables by reference, and which can take a variable number of arguments, and which can have local variables, and which don't necessarily need to be named (lambda functions)
- Completely automatic memory management

# Brief introduction to 'Rubish' through example programs
```
# Example: print text to the console
print "Hello";
print + 1 1;


# Example: store a value in a variable
set pi / 355 113;


# Example: count from 1 to 10
# Note: in rubish, the for loop counts from the start value, until it has met or gone beyond the stop value.
# That's why we write "10.1" here, so that the loop doesn't skip 10
for (counter 1 10.1) (
 print "Counting to ten:" counter;
)


# Example: loop while a condition is true
set n 10
while (> n 0) (
 print string$ n "o";
 set n - n 1
)


# Example: create and work with arrays
# This creates a 10x10 2D array
set matrix  array 10 10;
# We can set the contents of the array like this
for( x 0 10 ) (
 for( y 0 10 ) (
  set matrix[x y] * x y;
 )
)
# Then we can access contents in the array like this
print matrix[4 6];
set x-offset 3;
set y-offset 2;
print matrix[(+ 1 x-offset) (+ 3 y-offset)];
# This creates a 1D array with four items in it
set list  make-array "one" "two"  3  4.4;
# We can iterate over the array like this
foreach item list (
 print item;
)
# Or like this
for (i 0 (length list)) (
 print list[i];
)


# Example: define a function and call it
func celcius-to-fahrenheit ( temp-in-c ) (
 + 32 * temp-in-c / 9 5
)
print "37.1 celcius converted to fahrenheit:" celcius-to-fahrenheit 37.1;


# Example: define a function with parameter passed by reference.
# As well as passing variables, you can pass array cells too.
# If you specify a variable that doesn't exist, it will be created
func increment ( @ var ) (
 set var + var 1
)
print "Demo of the 'increment' function that we just defined:" increment mycounter increment mycounter increment mycounter;
print "After that, the value of 'mycounter' is:" mycounter;
set myarray make-array 11 22 33;
print "We can even use it for array cells:" increment myarray[0];
print "After doing that, the value of myarray[0] is:" myarray[0];


# Example: define a function that takes a variable number of arguments
func ascii-numbers-to-string ( . numbers ) (
 local result ("");
 foreach num numbers (
  set result cat$ result chr$ num
 )
 result
)
print "This should say 'hello':" (ascii-numbers-to-string 104 101 108 108 111) "also here is some other stuff" 123456;


# Example: Open a file for reading:
set f openin "/dev/urandom";
print get-byte f;
# Note that there is no command for closing files, because it's unnecessary: Rubish manages that. The file is closed once the last reference to it is deleted.
# This will close the file:
set f 0;


# Example: Open a file for writing, while also using a lambda function:
@(func(file)(
 # When the first argument of print is a fileport, it tries to print to that fileport. 
 print file "# Hello world";
 print file string-in-parens (
   print "This message being printed is proof that the code from 'our_test_text_file.txt' is being run";
   123456
 )
)) openout "our_test_text_file.txt";


# Example: execute Rubish code from an external file
set return-value source "our_test_text_file.txt";
print "return-value is " return-value;
```
