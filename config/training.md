<!-- Each step: plain lines are the lesson text, one per page. Settings under a step's heading:
  * require  what the step checks before it moves on, comma-separated: autotrim on|off, autopilot on|off,
             engine on|off, mode helm|trim|hoist|camera, wind LO..HI (degrees off the wind), speed LO..HI (m/s)
  * visit    LO..HI, a wind range to also hold once; every visit is needed
  * hold     seconds the goal must hold (default 2)
  * hint     shown when the player is not doing the step
  * hint_after  seconds off-task (after the last page is up) before the hint; above the first step = every step -->
hint_after: 15

# downwind_auto
require: autotrim on, wind 150..180
hint: Press B to turn on Auto Trim, and keep the wind behind you
You're sailing downwind
This is easy
Let's make it more fun :)
With a little help
Press the B button
This turns on Auto Trim
Notice the icon in the bottom HUD
You can press it again to turn it off.
You should keep it on for now
It makes sailing easier
We'll turn it off later when you learn more.

# beam_auto
require: autotrim on, wind 70..110
hint: Keep Auto Trim on (B) and turn until the wind is on your side
Let's sail upwind...
Now press up on the D-pad
This selects your HELM
Crank to turn the boat
With Auto Trim, your sail should change too

# points_auto
require: autotrim on, mode helm
visit: 45..70
visit: 110..150
hint: Press up to steer, then crank to a close reach and a broad reach
Try sailing upwind

# close_hauled
require: autotrim on, wind 35..50
hint: Turn closer to the wind, just before the sail starts to flap
You're now "close-hauled"
meaning you've sailed upwind perfectly
Let's try pushing it further

# irons
require: wind 0..30, speed ..0.3
hint: Point the bow straight into the wind and let the boat stop
Oh no! You're stuck in irons.
You're stuck going upwind
This is sometimes hard to get out of
We've given you an engine
It'll help get out of these pickles

# engine_free
require: engine on, wind 45..180, speed 0.5..
hint: Hold B to start the engine, then turn away from the wind
Hold down the B button and it'll start your engine
It's electric, don't worry
Press B again to turn it off
The engine doesn't have much power
We did this to cut down on weight for speed ;)

# downwind_manual
require: autotrim off, engine off, autopilot on, mode trim, wind 150..180
hint: Downwind, engine and Auto Trim off, A for autopilot, right to trim
Now that you're going downwind...
why don't you turn off auto trim?
Go ahead, you can do it \o/
I believe in you.
Press A to keep your heading automatically
Now press right on the D-pad
You're in sail trim mode.
The crank now adjusts the sail line
Try to maximize your wind
The heel of your boat is the first indicator
Your speed is your second
How is it going?
If you want to stop...
The pause menu has a go back feature
Oh, you're still here, awesome!
I'd recommend using Auto Trim in a pinch
But you'll go faster...
If you control the action


# beam_manual
require: autotrim off, engine off, wind 70..110
hint: Engine and Auto Trim off, wind at your side. Trim with the crank
Try going with the wind at your side
Notice that full in trim isn't the best?
It rarely is.


# points_manual
require: autotrim off, engine off
visit: 45..70
visit: 110..150
hint: Engine and Auto Trim off: try a close reach, then a broad reach
Let's push it more upwind!
Now try turning
Turning downwind is easier...
But takes more time.
You need to keep your momentum for good turns

# done
That's all I can teach you today
You'll have to sail the seas to learn more
