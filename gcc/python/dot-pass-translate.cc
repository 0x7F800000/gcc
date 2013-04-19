/* This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "gpython.h"

/*
  This pass should pass over the IL and preform translations
  to fold out var decls in classes and fold out polymorphism's

class Animal:
    def __init__(self, name):    # Constructor of the class
        self.name = name
    def talk(self):              # Abstract method, defined by convention only
        raise NotImplementedError("Subclass must implement abstract method")
 
class Cat(Animal):
    def talk(self):
        return 'Meow!'
 
class Dog(Animal):
    def talk(self):
        return 'Woof! Woof!'
 
animals = [Cat('Missy'),
           Cat('Mr. Mistoffelees'),
           Dog('Lassie')]
 
for animal in animals:
    print animal.name + ': ' + animal.talk()

...

Also fix how control structres look and iterators etc..

*/
vec<gpydot,va_gc> * dot_pass_translate (vec<gpydot,va_gc> * decls)
{
  return decls;
}
