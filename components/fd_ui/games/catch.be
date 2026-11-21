# Hung tu - tro choi dung san cua FDict One (firmware 4.9.5+), viet bang Berry.
# The tu roi ve phia hai cong dap an. Chon cong dung (nut hoac cham) truoc khi the cham cong.
# Luat nam o day; hoat hinh the (fd.move) va ve deu do C lam moi khung man hinh.
# Nut 1 2 = cong trai hoac tren, Nut 3 4 = cong phai hoac duoi.
import string

var W = fd.W
var H = fd.H
var side = fd.side
var vert = side == "left" || side == "right"
var N = fd.count()

def num(k, d)
  var v = fd.opt(k)
  return v == nil ? d : v
end

var dir = num("dir", 0)
var spd = num("spd", 1)
var fall = num("fall_ms", 6500)
var min_ms = num("min_ms", 1800)
var every = num("every", 5)
var ramp = num("ramp_pct", 90)
var lives = num("lives", 3)
if every < 1 every = 1 end
if lives < 1 lives = 1 end
if lives > 9 lives = 9 end
fall = fall * [135, 100, 75][spd % 3] / 100
if fall < min_ms fall = min_ms end

var C_GATE = 0xCBD5E1
var C_OK = 0x16A34A
var C_BAD = 0xDC2626
var C_TEXT = 0x111827
var CARD_BG = 0xFEF3C7
var CARD_BD = 0xF59E0B

var score = 0
var combo = 0
var right = 0
var level = 1
var running = false
var wait = 0
var ans = 0
var cur = 0
var order = []
var pos = 0
var gx = [0, 0]
var gy = [0, 0]
var gw = 0
var gh = 0
var gfont = 18

# id: 0 the tu, 1 cong 0, 2 cong 1
def gates()
  if !vert
    gh = 58
    gw = W / 2 - 6
    var y = side == "bottom" ? H - gh - 4 : 4
    gx = [4, W - gw - 4]
    gy = [y, y]
  else
    gw = 104
    gh = (H - 8 - 6) / 2
    var x = side == "right" ? W - gw - 4 : 4
    gx = [x, x]
    gy = [4, 4 + gh + 6]
    gfont = 14
  end
  for g : 0 .. 1
    fd.box(g + 1, gx[g], gy[g], gw, gh)
    fd.font(g + 1, gfont)
    fd.style(g + 1, 3, C_GATE, 12, 4)
    fd.color(g + 1, 0xFFFFFF, C_TEXT)
    fd.show(g + 1, true)
  end
end

def gate_paint(g, border, bg)
  fd.style(g + 1, 3, border, 12, 4)
  fd.color(g + 1, bg, C_TEXT)
end

def shuffle()
  order = []
  for i : 0 .. N - 1 order.push(i) end
  var i = N - 1
  while i > 0
    var j = fd.rand(i + 1)
    var t = order[i]
    order[i] = order[j]
    order[j] = t
    i -= 1
  end
  pos = 0
end

def pick()
  if pos >= N shuffle() end
  var k = order[pos]
  pos += 1
  return k
end

def next_card()
  cur = pick()
  var card_vi = dir == 0 || (dir == 2 && fd.rand(2) == 0)
  var card = card_vi ? fd.vi(cur) : fd.en(cur)
  var good = card_vi ? fd.en(cur) : fd.vi(cur)
  var bad = good
  var tries = 0
  while tries < 30
    var k = fd.rand(N)
    var t = card_vi ? fd.en(k) : fd.vi(k)
    if k != cur && string.tolower(t) != string.tolower(good)
      bad = t
      break
    end
    tries += 1
  end
  ans = fd.rand(2)
  fd.text(ans + 1, good)
  fd.text(2 - ans, bad)
  gate_paint(0, C_GATE, 0xFFFFFF)
  gate_paint(1, C_GATE, 0xFFFFFF)

  # kich thuoc the theo chu, roi duong chay toi sat cong
  var pad = 6
  var bw = 2
  var maxw = W - 40
  if vert
    maxw = side == "right" ? gx[0] - 12 : W - (gx[0] + gw) - 12
  end
  var sz = fd.measure(card, 18, maxw - 2 * (pad + bw))
  var cw = sz[0] + 2 * (pad + bw) + 2
  var ch = sz[1] + 2 * (pad + bw)
  var x0 = 0
  var y0 = 0
  var x1 = 0
  var y1 = 0
  if side == "bottom"
    x0 = (W - cw) / 2
    x1 = x0
    y0 = 2
    y1 = gy[0] - ch - 2
  elif side == "top"
    x0 = (W - cw) / 2
    x1 = x0
    y0 = H - ch - 2
    y1 = gy[0] + gh + 2
  elif side == "right"
    y0 = (H - ch) / 2
    y1 = y0
    x0 = 4
    x1 = gx[0] - cw - 4
  else
    y0 = (H - ch) / 2
    y1 = y0
    x0 = W - cw - 4
    x1 = gx[0] + gw + 4
  end
  if (side == "bottom" && y1 < y0) || (side == "top" && y1 > y0) y1 = y0 end
  if (side == "right" && x1 < x0) || (side == "left" && x1 > x0) x1 = x0 end
  fd.box(0, x0, y0, cw, 0)
  fd.text(0, card)
  fd.style(0, bw, CARD_BD, 10, pad)
  fd.color(0, CARD_BG, C_TEXT)
  fd.show(0, true)
  fd.move(0, x1, y1, fall)
  running = true
end

def hud()
  fd.hud(score, lives, level, combo > 5 ? 5 : combo)
end

# ok: chon dung; chosen: cong da chon (-1 = het gio)
def feed(ok, chosen)
  running = false
  fd.stop(0)
  fd.show(0, false)
  gate_paint(ans, C_OK, ok ? 0xDCFCE7 : 0xFFFFFF)
  if ok
    combo += 1
    right += 1
    score += 10 * (combo > 5 ? 5 : combo)
    if right % every == 0
      level += 1
      fall = fall * ramp / 100
      if fall < min_ms fall = min_ms end
    end
    fd.say(fd.en(cur))
    wait = 450
  else
    if chosen >= 0 gate_paint(chosen, C_BAD, 0xFEE2E2) end
    combo = 0
    lives -= 1
    fd.sound(false)
    wait = 1100
  end
  hud()
end

def choose(g)
  if running feed(g == ans, g) end
end

def init()
  gates()
  shuffle()
  hud()
  next_card()
end

def arrive(id)
  if id == 0 && running feed(false, -1) end
end

def button(n)
  choose(n <= 2 ? 0 : 1)
end

def tap(id)
  if id == 1 || id == 2 choose(id - 1) end
end

def tick(dt)
  if running || wait <= 0 return end
  wait -= dt
  if wait <= 0
    wait = 0
    if lives <= 0
      fd.over(score, right, level)
    else
      next_card()
    end
  end
end
