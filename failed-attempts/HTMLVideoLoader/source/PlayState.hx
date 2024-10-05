package;

import flixel.FlxG;
import flixel.FlxState;
import js.html.MessageEvent;

using StringTools;

enum Instruction
{
	NONE;
	INIT_VIDEO;
	SET_VIDEO_TIME;
}

class PlayState extends FlxState
{
	var video:FlxVideo;

	override public function create()
	{
		super.create();
		FlxG.mouse.visible = false;
		js.Browser.window.addEventListener("message", onMessageReceived);
	}

	private function onMessageReceived(event:MessageEvent):Void
	{
		// Retrieve the data sent from the WebView
		var data = event.data;

		// Process the received data (e.g., if it's a string)
		if (Std.isOfType(data, String))
		{
			handleReceivedString(data);
		}
	}

	private function handleReceivedString(data:String):Void
	{
		var tokens = data.split(':');
		var nextInstruction:Instruction = NONE;
		for (tokenID in 0...tokens.length)
		{
			var token = tokens[tokenID];
			if (tokenID == 0)
			{
				if (token == 'initVideo')
					nextInstruction = INIT_VIDEO;
				if (token == 'setVideoTime')
					nextInstruction = SET_VIDEO_TIME;
				if (video != null)
				{
					if (token == 'playVideo')
						video.play();
					if (token == 'pauseVideo')
						video.pause();
					if (token == 'stopVideo')
						video.stop();
				}
			}
			else
			{
				if (nextInstruction == INIT_VIDEO)
				{
					video = new FlxVideo({source: token});
					add(video);
				}
				if (nextInstruction == SET_VIDEO_TIME && video != null)
					video.seek(Std.parseFloat(token));
			}
		}
	}
}
