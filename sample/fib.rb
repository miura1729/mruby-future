def fib(n)
  if n > 37 then
    a = Future.new(n) {|n| fib(n - 1) }.value
    fib(n - 2) + a.value
  elsif n < 2 then
    1
  else
    fib(n - 2) + fib(n - 1)
  end
end

p fib(39)


