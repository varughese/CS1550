class Animal():
	def __init__(self, legs, name):
		self.legs = legs
		self.name = name
	
	def eat(self):
		print(self.name, " is eating")
	
	def speak(self):
		print("speaking")

class Dog(Animal):
	def speak(self):
		print("bark")

class Cow(Animal):
	def speak(self):
		print("moo")

class Pig(Animal):
	def speak(self):
		print("oink")


animals = [
	Dog(4, "Fido"), 
	Cow(4, "Oscar"), 
	Pig(4, "Charlotte")
]

for animal in animals:
	animal.speak()

