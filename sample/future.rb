a = Future.new(1000) {|b| j = 1; 1000.times {|i| j += i}; j}
p a
p a.state
p a.peek
p a.value
p a.state
