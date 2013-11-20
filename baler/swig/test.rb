#!/usr/bin/env ruby
require './bset_wrap'
include Bset_wrap

def test_exist(x, num)
	if (bset_u32_exist(x, num) != 0)
		puts "#{num} exists."
	else
		puts "#{num} does not exists."
	end
end

x = bset_u32_alloc(20)

bset_u32_insert(x, 5)
test_exist(x, 1)
test_exist(x, 5)


