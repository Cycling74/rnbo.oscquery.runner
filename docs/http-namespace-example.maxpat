{
	"patcher" : 	{
		"fileversion" : 1,
		"appversion" : 		{
			"major" : 8,
			"minor" : 1,
			"revision" : 5,
			"architecture" : "x64",
			"modernui" : 1
		}
,
		"classnamespace" : "box",
		"rect" : [ 204.0, 170.0, 422.0, 699.0 ],
		"bglocked" : 0,
		"openinpresentation" : 0,
		"default_fontsize" : 12.0,
		"default_fontface" : 0,
		"default_fontname" : "Arial",
		"gridonopen" : 1,
		"gridsize" : [ 15.0, 15.0 ],
		"gridsnaponopen" : 1,
		"objectsnaponopen" : 1,
		"statusbarvisible" : 2,
		"toolbarvisible" : 1,
		"lefttoolbarpinned" : 0,
		"toptoolbarpinned" : 0,
		"righttoolbarpinned" : 0,
		"bottomtoolbarpinned" : 0,
		"toolbars_unpinned_last_save" : 0,
		"tallnewobj" : 0,
		"boxanimatetime" : 200,
		"enablehscroll" : 1,
		"enablevscroll" : 1,
		"devicewidth" : 0.0,
		"description" : "",
		"digest" : "",
		"tags" : "",
		"style" : "",
		"subpatcher_template" : "",
		"assistshowspatchername" : 0,
		"boxes" : [ 			{
				"box" : 				{
					"id" : "obj-4",
					"linecount" : 5,
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 69.0, 39.0, 150.0, 74.0 ],
					"text" : "example rnbo patch to expose features in OSCQuery runner for documentation of HTTP namespace"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-2",
					"maxclass" : "ezdac~",
					"numinlets" : 2,
					"numoutlets" : 0,
					"patching_rect" : [ 73.0, 213.0, 45.0, 45.0 ]
				}

			}
, 			{
				"box" : 				{
					"autosave" : 1,
					"id" : "obj-1",
					"maxclass" : "newobj",
					"midiinletcount" : 1,
					"midioutletcount" : 1,
					"numinlets" : 2,
					"numoutlets" : 5,
					"outlettype" : [ "signal", "signal", "signal", "int", "list" ],
					"patcher" : 					{
						"fileversion" : 1,
						"appversion" : 						{
							"major" : 8,
							"minor" : 1,
							"revision" : 5,
							"architecture" : "x64",
							"modernui" : 1
						}
,
						"classnamespace" : "rnbo",
						"rect" : [ 204.0, 170.0, 2175.0, 898.0 ],
						"bglocked" : 0,
						"openinpresentation" : 0,
						"default_fontsize" : 12.0,
						"default_fontface" : 0,
						"default_fontname" : "Lato",
						"gridonopen" : 1,
						"gridsize" : [ 15.0, 15.0 ],
						"gridsnaponopen" : 1,
						"objectsnaponopen" : 1,
						"statusbarvisible" : 2,
						"toolbarvisible" : 1,
						"lefttoolbarpinned" : 0,
						"toptoolbarpinned" : 0,
						"righttoolbarpinned" : 0,
						"bottomtoolbarpinned" : 0,
						"toolbars_unpinned_last_save" : 0,
						"tallnewobj" : 0,
						"boxanimatetime" : 200,
						"enablehscroll" : 1,
						"enablevscroll" : 1,
						"devicewidth" : 0.0,
						"description" : "",
						"digest" : "",
						"tags" : "",
						"style" : "",
						"subpatcher_template" : "",
						"assistshowspatchername" : 0,
						"boxes" : [ 							{
								"box" : 								{
									"id" : "obj-20",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 3,
									"outlettype" : [ "", "", "" ],
									"patching_rect" : [ 981.0, 72.0, 90.0, 23.0 ],
									"rnbo_serial" : 3,
									"rnbo_uniqueid" : "data_obj-20",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"info" : 											{
												"attrOrProp" : 1,
												"digest" : "Bang to report buffer information.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "bang"
											}
,
											"sizeout" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Samples",
												"defaultarg" : 2,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"chanout" : 											{
												"attrOrProp" : 1,
												"digest" : "Number of Channels",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"srout" : 											{
												"attrOrProp" : 1,
												"digest" : "Sample rate",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"size" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Samples. Take care when setting, allocation might block audio processing.",
												"defaultarg" : 2,
												"isalias" : 0,
												"aliases" : [ "samples" ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"samples" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Samples. Take care when setting, allocation might block audio processing.",
												"defaultarg" : 2,
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"sizems" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Milliseconds. Take care when setting, allocation might block audio processing.",
												"isalias" : 0,
												"aliases" : [ "ms" ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"ms" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Milliseconds. Take care when setting, allocation might block audio processing.",
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"channels" : 											{
												"attrOrProp" : 1,
												"digest" : "Change channel count. Take care when setting, allocation might block audio processing.",
												"defaultarg" : 3,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"name" : 											{
												"attrOrProp" : 2,
												"digest" : "Name of data object or external buffer",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}
,
											"file" : 											{
												"attrOrProp" : 2,
												"digest" : "File name/path or URL to load into buffer.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"type" : 											{
												"attrOrProp" : 2,
												"digest" : "Type of Data (Float32, Float64)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"samplerate" : 											{
												"attrOrProp" : 2,
												"digest" : "Sample rate",
												"defaultarg" : 4,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"fill" : 											{
												"attrOrProp" : 2,
												"digest" : "Fill expression, this could be a value, or a simple function like sin(x), where x will run from 0 to 1 to fill the buffer.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"external" : 											{
												"attrOrProp" : 2,
												"digest" : "Await data from the outside world, buffers with no size are always external.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "false"
											}

										}
,
										"inputs" : [ 											{
												"name" : "info",
												"type" : "bang",
												"digest" : "Bang to report buffer information.",
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "sizeout",
												"type" : "number",
												"digest" : "Size in Samples",
												"defaultarg" : 2,
												"docked" : 0
											}
, 											{
												"name" : "chanout",
												"type" : "number",
												"digest" : "Number of Channels",
												"docked" : 0
											}
, 											{
												"name" : "srout",
												"type" : "number",
												"digest" : "Sample rate",
												"docked" : 0
											}
 ],
										"helpname" : "buffer~",
										"aliasOf" : "data",
										"classname" : "data",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : 2013683893
									}
,
									"text" : "buffer~ myloop"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-6",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 2,
									"outlettype" : [ "", "" ],
									"patching_rect" : [ 635.0, 76.0, 143.0, 23.0 ],
									"rnbo_serial" : 2,
									"rnbo_uniqueid" : "mode",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"value" : 											{
												"attrOrProp" : 1,
												"digest" : "Parameter value",
												"defaultarg" : 2,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 1,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"reset" : 											{
												"attrOrProp" : 1,
												"digest" : "Reset param to initial value",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 1,
												"isparam" : 0,
												"type" : "bang"
											}
,
											"normalized" : 											{
												"attrOrProp" : 1,
												"digest" : "Normalized parameter value.",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"name" : 											{
												"attrOrProp" : 2,
												"digest" : "Parameter Name",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"id" : 											{
												"attrOrProp" : 2,
												"digest" : "DEPRECATED",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"enum" : 											{
												"attrOrProp" : 2,
												"digest" : "Enum Values",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"minimum" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter mininum",
												"isalias" : 0,
												"aliases" : [ "min" ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"min" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter mininum",
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"maximum" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter maximum",
												"isalias" : 0,
												"aliases" : [ "max" ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"max" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter maximum",
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"exponent" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter exponent",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"steps" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter steps",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"displayname" : 											{
												"attrOrProp" : 2,
												"digest" : "Display Name",
												"isalias" : 0,
												"aliases" : [ "displayName" ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "symbol",
												"defaultValue" : ""
											}
,
											"displayName" : 											{
												"attrOrProp" : 2,
												"digest" : "Display Name",
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "symbol",
												"defaultValue" : ""
											}
,
											"unit" : 											{
												"attrOrProp" : 2,
												"digest" : "Unit",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "symbol",
												"defaultValue" : ""
											}
,
											"rect" : 											{
												"attrOrProp" : 2,
												"digest" : "layout rect",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "list",
												"defaultValue" : ""
											}
,
											"style" : 											{
												"attrOrProp" : 2,
												"digest" : "box style",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"enum" : [ "none", "button", "dial", "number", "slider", "toggle" ],
												"type" : "enum",
												"defaultValue" : "slider"
											}
,
											"tonormalized" : 											{
												"attrOrProp" : 2,
												"digest" : "Custom Scaling - convert to normalized (0..1) form.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"fromnormalized" : 											{
												"attrOrProp" : 2,
												"digest" : "Custom Scaling - convert from normalized (0..1) form.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"order" : 											{
												"attrOrProp" : 2,
												"digest" : "Restore order.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"noinit" : 											{
												"attrOrProp" : 2,
												"digest" : "Do not send initial value.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"nopreset" : 											{
												"attrOrProp" : 2,
												"digest" : "Do not add this value to the preset [DEPRECATED - USE @preset 0 instead].",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"preset" : 											{
												"attrOrProp" : 2,
												"digest" : "Add this value to the preset.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}

										}
,
										"inputs" : [ 											{
												"name" : "value",
												"type" : "number",
												"digest" : "Parameter value",
												"defaultarg" : 2,
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "value",
												"type" : "number",
												"digest" : "Parameter value",
												"defaultarg" : 2,
												"hot" : 1,
												"docked" : 0
											}
, 											{
												"name" : "normalized",
												"type" : "number",
												"digest" : "Normalized parameter value.",
												"docked" : 0
											}
 ],
										"helpname" : "param",
										"aliasOf" : "param",
										"classname" : "param",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : -579982699
									}
,
									"text" : "param mode @enum x y z",
									"varname" : "mode"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-23",
									"maxclass" : "message",
									"numinlets" : 2,
									"numoutlets" : 1,
									"outlettype" : [ "" ],
									"patching_rect" : [ 474.0, 240.0, 29.5, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "message_obj-23",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"bangval" : 											{
												"attrOrProp" : 1,
												"digest" : "Trigger the Message.",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 1,
												"type" : "bang"
											}
,
											"set" : 											{
												"attrOrProp" : 1,
												"digest" : "Set inlet - not yet supported.",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "bang"
											}
,
											"out" : 											{
												"attrOrProp" : 1,
												"digest" : "Message out.",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "list"
											}
,
											"text" : 											{
												"attrOrProp" : 2,
												"digest" : "text",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "list",
												"defaultValue" : ""
											}

										}
,
										"inputs" : [ 											{
												"name" : "bangval",
												"type" : "bang",
												"digest" : "Trigger the Message.",
												"hot" : 1,
												"docked" : 0
											}
, 											{
												"name" : "set",
												"type" : "bang",
												"digest" : "Set inlet - not yet supported.",
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "out",
												"type" : "list",
												"digest" : "Message out.",
												"docked" : 0
											}
 ],
										"helpname" : "message",
										"classname" : "message",
										"digest" : "message box",
										"versionId" : -1483348003,
										"expressive" : 0,
										"extraattrs" : 										{
											"bangval" : 0
										}

									}
,
									"text" : "20"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-19",
									"maxclass" : "newobj",
									"numinlets" : 5,
									"numoutlets" : 1,
									"outlettype" : [ "" ],
									"patching_rect" : [ 474.0, 303.0, 61.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "noteout_obj-19",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 0,
										"argnames" : 										{
											"notenumber" : 											{
												"attrOrProp" : 1,
												"digest" : "note number",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"velocity" : 											{
												"attrOrProp" : 1,
												"digest" : "velocity (0 - 127)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"releasevelocity" : 											{
												"attrOrProp" : 1,
												"digest" : "release velocity (0 - 127)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"channel" : 											{
												"attrOrProp" : 1,
												"digest" : "MIDI channel (1 - 16)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"port" : 											{
												"attrOrProp" : 1,
												"digest" : "select port",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"midiout" : 											{
												"attrOrProp" : 1,
												"digest" : "connect to midiout",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}

										}
,
										"inputs" : [ 											{
												"name" : "notenumber",
												"type" : "number",
												"digest" : "note number",
												"hot" : 1,
												"docked" : 0
											}
, 											{
												"name" : "velocity",
												"type" : "number",
												"digest" : "velocity (0 - 127)",
												"docked" : 0
											}
, 											{
												"name" : "releasevelocity",
												"type" : "number",
												"digest" : "release velocity (0 - 127)",
												"docked" : 0
											}
, 											{
												"name" : "channel",
												"type" : "number",
												"digest" : "MIDI channel (1 - 16)",
												"docked" : 0
											}
, 											{
												"name" : "port",
												"type" : "number",
												"digest" : "select port",
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "midiout",
												"type" : "number",
												"digest" : "connect to midiout",
												"docked" : 0
											}
 ],
										"helpname" : "noteout",
										"classname" : "noteout",
										"digest" : "generate MIDI note messages",
										"versionId" : 0,
										"expressive" : 0
									}
,
									"text" : "noteout"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-18",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 0,
									"patching_rect" : [ 365.0, 318.0, 43.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "signaloutlet_obj-18",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"in1" : 											{
												"attrOrProp" : 1,
												"digest" : "signal sent to outlet with index 3",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"index" : 											{
												"attrOrProp" : 2,
												"digest" : "outlet number",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}
,
											"comment" : 											{
												"attrOrProp" : 2,
												"digest" : "mouse over comment",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}

										}
,
										"inputs" : [ 											{
												"name" : "in1",
												"type" : "signal",
												"digest" : "signal sent to outlet with index 3",
												"displayName" : "",
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [  ],
										"helpname" : "out~",
										"aliasOf" : "out~",
										"classname" : "signaloutlet",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : 0
									}
,
									"text" : "out~ 3"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-17",
									"maxclass" : "newobj",
									"numinlets" : 3,
									"numoutlets" : 2,
									"outlettype" : [ "signal", "signal" ],
									"patching_rect" : [ 365.0, 283.0, 73.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "groove_obj-17",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"begin" : 											{
												"attrOrProp" : 1,
												"digest" : "Loop min (ms).",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"end" : 											{
												"attrOrProp" : 1,
												"digest" : "Loop max (ms).",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "-1"
											}
,
											"out1" : 											{
												"attrOrProp" : 1,
												"digest" : "out1",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"sync" : 											{
												"attrOrProp" : 1,
												"digest" : "Sync output",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"loop" : 											{
												"attrOrProp" : 1,
												"digest" : "loop",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"crossfade" : 											{
												"attrOrProp" : 1,
												"digest" : "Attempted crossfade on loop jumpback, actual fade depends on available sample material. (ms)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"stop" : 											{
												"attrOrProp" : 1,
												"digest" : "stop",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 1,
												"isparam" : 0,
												"type" : "bang"
											}
,
											"buffer" : 											{
												"attrOrProp" : 1,
												"digest" : "buffer",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"buffername" : 											{
												"attrOrProp" : 2,
												"digest" : "Buffer to use.",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}
,
											"channels" : 											{
												"attrOrProp" : 2,
												"digest" : "Number of channels to read.",
												"defaultarg" : 2,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}

										}
,
										"inputs" : [ 											{
												"name" : "rate",
												"type" : [ "bang", "auto" ],
												"digest" : "Playback rate.",
												"hot" : 1,
												"docked" : 0
											}
, 											{
												"name" : "begin",
												"type" : "auto",
												"digest" : "Loop min (ms).",
												"docked" : 0
											}
, 											{
												"name" : "end",
												"type" : "auto",
												"digest" : "Loop max (ms).",
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "out1",
												"type" : "signal",
												"digest" : "out1",
												"docked" : 0
											}
, 											{
												"name" : "sync",
												"type" : "signal",
												"digest" : "Sync output",
												"docked" : 0
											}
 ],
										"helpname" : "groove~",
										"aliasOf" : "groove~",
										"classname" : "groove",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : 97022002
									}
,
									"text" : "groove~ baz"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-16",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 3,
									"outlettype" : [ "", "", "" ],
									"patching_rect" : [ 816.0, 72.0, 148.0, 23.0 ],
									"rnbo_serial" : 2,
									"rnbo_uniqueid" : "data_obj-16",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"info" : 											{
												"attrOrProp" : 1,
												"digest" : "Bang to report buffer information.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "bang"
											}
,
											"sizeout" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Samples",
												"defaultarg" : 2,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"chanout" : 											{
												"attrOrProp" : 1,
												"digest" : "Number of Channels",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"srout" : 											{
												"attrOrProp" : 1,
												"digest" : "Sample rate",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"size" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Samples. Take care when setting, allocation might block audio processing.",
												"defaultarg" : 2,
												"isalias" : 0,
												"aliases" : [ "samples" ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"samples" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Samples. Take care when setting, allocation might block audio processing.",
												"defaultarg" : 2,
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"sizems" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Milliseconds. Take care when setting, allocation might block audio processing.",
												"isalias" : 0,
												"aliases" : [ "ms" ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"ms" : 											{
												"attrOrProp" : 1,
												"digest" : "Size in Milliseconds. Take care when setting, allocation might block audio processing.",
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"channels" : 											{
												"attrOrProp" : 1,
												"digest" : "Change channel count. Take care when setting, allocation might block audio processing.",
												"defaultarg" : 3,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"name" : 											{
												"attrOrProp" : 2,
												"digest" : "Name of data object or external buffer",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}
,
											"file" : 											{
												"attrOrProp" : 2,
												"digest" : "File name/path or URL to load into buffer.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"type" : 											{
												"attrOrProp" : 2,
												"digest" : "Type of Data (Float32, Float64)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"samplerate" : 											{
												"attrOrProp" : 2,
												"digest" : "Sample rate",
												"defaultarg" : 4,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"fill" : 											{
												"attrOrProp" : 2,
												"digest" : "Fill expression, this could be a value, or a simple function like sin(x), where x will run from 0 to 1 to fill the buffer.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"external" : 											{
												"attrOrProp" : 2,
												"digest" : "Await data from the outside world, buffers with no size are always external.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "false"
											}

										}
,
										"inputs" : [ 											{
												"name" : "info",
												"type" : "bang",
												"digest" : "Bang to report buffer information.",
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "sizeout",
												"type" : "number",
												"digest" : "Size in Samples",
												"defaultarg" : 2,
												"docked" : 0
											}
, 											{
												"name" : "chanout",
												"type" : "number",
												"digest" : "Number of Channels",
												"docked" : 0
											}
, 											{
												"name" : "srout",
												"type" : "number",
												"digest" : "Sample rate",
												"docked" : 0
											}
 ],
										"helpname" : "buffer~",
										"aliasOf" : "data",
										"classname" : "data",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : 2013683893
									}
,
									"text" : "buffer~ baz @file anton.aif"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-15",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 0,
									"patching_rect" : [ 188.0, 322.0, 43.0, 23.0 ],
									"rnbo_serial" : 2,
									"rnbo_uniqueid" : "signaloutlet_obj-15",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"in1" : 											{
												"attrOrProp" : 1,
												"digest" : "signal sent to outlet with index 2",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"index" : 											{
												"attrOrProp" : 2,
												"digest" : "outlet number",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}
,
											"comment" : 											{
												"attrOrProp" : 2,
												"digest" : "mouse over comment",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}

										}
,
										"inputs" : [ 											{
												"name" : "in1",
												"type" : "signal",
												"digest" : "signal sent to outlet with index 2",
												"displayName" : "",
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [  ],
										"helpname" : "out~",
										"aliasOf" : "out~",
										"classname" : "signaloutlet",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : 0
									}
,
									"text" : "out~ 2"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-14",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 2,
									"outlettype" : [ "signal", "signal" ],
									"patching_rect" : [ 188.0, 283.0, 42.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "cycle~_obj-14",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 0,
										"argnames" : 										{
											"buffer" : 											{
												"attrOrProp" : 1,
												"digest" : "buffer",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"out1" : 											{
												"attrOrProp" : 1,
												"digest" : "out1",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"out2" : 											{
												"attrOrProp" : 1,
												"digest" : "out2",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"frequency" : 											{
												"attrOrProp" : 1,
												"digest" : "Frequency or phase",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"reset" : 											{
												"attrOrProp" : 1,
												"digest" : "reset",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 1,
												"isparam" : 0,
												"type" : "bang",
												"defaultValue" : "0"
											}
,
											"index" : 											{
												"attrOrProp" : 2,
												"digest" : "index mode, freq/phase for being driven by frequency or a phasor",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"enum" : [ "freq", "phase" ],
												"type" : "enum",
												"defaultValue" : "freq"
											}
,
											"interp" : 											{
												"attrOrProp" : 2,
												"digest" : "interpolation mode",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"enum" : [ "linear", "cubic", "spline", "cosine", "step", "none" ],
												"type" : "enum",
												"defaultValue" : "linear"
											}
,
											"buffername" : 											{
												"attrOrProp" : 2,
												"digest" : "buffer to read the wavetable from (default: sinus)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "symbol",
												"defaultValue" : "RNBODefaultSinus"
											}

										}
,
										"inputs" : [ 											{
												"name" : "frequency",
												"type" : "auto",
												"digest" : "Frequency or phase",
												"displayName" : "frequency",
												"defaultarg" : 1,
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "out1",
												"type" : "signal",
												"digest" : "out1",
												"displayName" : "out",
												"docked" : 0
											}
, 											{
												"name" : "out2",
												"type" : "signal",
												"digest" : "out2",
												"displayName" : "phase",
												"docked" : 0
											}
 ],
										"helpname" : "cycle~",
										"classname" : "cycle~",
										"digest" : "cycle~",
										"expressive" : 0
									}
,
									"text" : "cycle~"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-13",
									"maxclass" : "newobj",
									"numinlets" : 2,
									"numoutlets" : 1,
									"outlettype" : [ "" ],
									"patching_rect" : [ 188.0, 186.0, 33.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "expr_obj-13",
									"rnboinfo" : 									{
										"parseOp" : 1,
										"argnames" : 										{
											"tuning" : 											{
												"attrOrProp" : 1,
												"digest" : "Tuning in Hz",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"hires" : 											{
												"attrOrProp" : 2,
												"digest" : "High Resolution",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"hot" : 											{
												"type" : 1,
												"digest" : "All inlets trigger calculation."
											}

										}
,
										"inputs" : [ 											{
												"name" : "in1",
												"type" : "number",
												"hot" : 1,
												"displayName" : "midivalue",
												"digest" : "Midi value to convert"
											}
, 											{
												"name" : "in2",
												"type" : "number",
												"hot" : 0,
												"displayName" : "tuning",
												"digest" : "Tuning in Hz"
											}
 ],
										"outputs" : [ 											{
												"name" : "out1",
												"type" : "number",
												"displayName" : "frequency",
												"digest" : "Converted Frequency"
											}
 ],
										"classname" : "expr",
										"expressive" : 1,
										"finalize" : 0,
										"digest" : "convert MIDI note to frequency",
										"helpname" : "mtof"
									}
,
									"text" : "mtof"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-12",
									"maxclass" : "newobj",
									"numinlets" : 2,
									"numoutlets" : 4,
									"outlettype" : [ "", "", "", "" ],
									"patching_rect" : [ 188.0, 120.0, 50.5, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "notein_obj-12",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 0,
										"argnames" : 										{
											"notenumber" : 											{
												"attrOrProp" : 1,
												"digest" : "note number (0-127)",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"velocity" : 											{
												"attrOrProp" : 1,
												"digest" : "velocity (0-127)",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"releasevelocity" : 											{
												"attrOrProp" : 1,
												"digest" : "release velocity (0-127)",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"outchannel" : 											{
												"attrOrProp" : 1,
												"digest" : "MIDI channel (1-16)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"input" : 											{
												"attrOrProp" : 1,
												"digest" : "MIDI Input",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"channel" : 											{
												"attrOrProp" : 1,
												"digest" : "MIDI channel (1-16) to output (-1 for all)",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}

										}
,
										"inputs" : [ 											{
												"name" : "input",
												"type" : "number",
												"digest" : "MIDI Input",
												"hot" : 1,
												"docked" : 0
											}
, 											{
												"name" : "channel",
												"type" : "number",
												"digest" : "MIDI channel (1-16) to output (-1 for all)",
												"defaultarg" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "notenumber",
												"type" : "number",
												"digest" : "note number (0-127)",
												"docked" : 0
											}
, 											{
												"name" : "velocity",
												"type" : "number",
												"digest" : "velocity (0-127)",
												"docked" : 0
											}
, 											{
												"name" : "releasevelocity",
												"type" : "number",
												"digest" : "release velocity (0-127)",
												"docked" : 0
											}
, 											{
												"name" : "outchannel",
												"type" : "number",
												"digest" : "MIDI channel (1-16)",
												"docked" : 0
											}
 ],
										"helpname" : "notein",
										"classname" : "notein",
										"digest" : "MIDI note input",
										"versionId" : 0,
										"expressive" : 0
									}
,
									"text" : "notein"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-11",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 0,
									"patching_rect" : [ 467.0, 168.0, 115.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "outport_obj-11",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 0,
										"argnames" : 										{
											"tag" : 											{
												"attrOrProp" : 2,
												"digest" : "Tag to send messages to.",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}

										}
,
										"inputs" : [ 											{
												"name" : "input",
												"type" : [ "bang", "number", "list" ],
												"digest" : "number or list to send out.",
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [  ],
										"helpname" : "outport",
										"classname" : "outport",
										"digest" : "outport",
										"versionId" : 0,
										"expressive" : 0
									}
,
									"text" : "outport /tagged/out"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-10",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 0,
									"patching_rect" : [ 396.0, 201.0, 69.0, 23.0 ],
									"rnbo_serial" : 2,
									"rnbo_uniqueid" : "outport_obj-10",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 0,
										"argnames" : 										{
											"tag" : 											{
												"attrOrProp" : 2,
												"digest" : "Tag to send messages to.",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}

										}
,
										"inputs" : [ 											{
												"name" : "input",
												"type" : [ "bang", "number", "list" ],
												"digest" : "number or list to send out.",
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [  ],
										"helpname" : "outport",
										"classname" : "outport",
										"digest" : "outport",
										"versionId" : 0,
										"expressive" : 0
									}
,
									"text" : "outport bar"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-9",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 0,
									"patching_rect" : [ 527.0, 130.0, 33.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "print_obj-9",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"prefix" : 											{
												"attrOrProp" : 2,
												"digest" : "printed first",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}

										}
,
										"inputs" : [ 											{
												"name" : "input",
												"type" : [ "number", "list", "bang" ],
												"digest" : "value to be printed",
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [  ],
										"helpname" : "print",
										"aliasOf" : "print",
										"classname" : "print",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : -37461495
									}
,
									"text" : "print"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-8",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 2,
									"outlettype" : [ "signal", "signal" ],
									"patching_rect" : [ 104.0, 283.0, 42.0, 23.0 ],
									"rnbo_serial" : 2,
									"rnbo_uniqueid" : "cycle~_obj-8",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 0,
										"argnames" : 										{
											"buffer" : 											{
												"attrOrProp" : 1,
												"digest" : "buffer",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 1,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"out1" : 											{
												"attrOrProp" : 1,
												"digest" : "out1",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"out2" : 											{
												"attrOrProp" : 1,
												"digest" : "out2",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"frequency" : 											{
												"attrOrProp" : 1,
												"digest" : "Frequency or phase",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"reset" : 											{
												"attrOrProp" : 1,
												"digest" : "reset",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 1,
												"isparam" : 0,
												"type" : "bang",
												"defaultValue" : "0"
											}
,
											"index" : 											{
												"attrOrProp" : 2,
												"digest" : "index mode, freq/phase for being driven by frequency or a phasor",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"enum" : [ "freq", "phase" ],
												"type" : "enum",
												"defaultValue" : "freq"
											}
,
											"interp" : 											{
												"attrOrProp" : 2,
												"digest" : "interpolation mode",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"enum" : [ "linear", "cubic", "spline", "cosine", "step", "none" ],
												"type" : "enum",
												"defaultValue" : "linear"
											}
,
											"buffername" : 											{
												"attrOrProp" : 2,
												"digest" : "buffer to read the wavetable from (default: sinus)",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "symbol",
												"defaultValue" : "RNBODefaultSinus"
											}

										}
,
										"inputs" : [ 											{
												"name" : "frequency",
												"type" : "auto",
												"digest" : "Frequency or phase",
												"displayName" : "frequency",
												"defaultarg" : 1,
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "out1",
												"type" : "signal",
												"digest" : "out1",
												"displayName" : "out",
												"docked" : 0
											}
, 											{
												"name" : "out2",
												"type" : "signal",
												"digest" : "out2",
												"displayName" : "phase",
												"docked" : 0
											}
 ],
										"helpname" : "cycle~",
										"classname" : "cycle~",
										"digest" : "cycle~",
										"expressive" : 0
									}
,
									"text" : "cycle~"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-7",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 2,
									"outlettype" : [ "", "" ],
									"patching_rect" : [ 104.0, 76.0, 264.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "freq",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"value" : 											{
												"attrOrProp" : 1,
												"digest" : "Parameter value",
												"defaultarg" : 2,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 1,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"reset" : 											{
												"attrOrProp" : 1,
												"digest" : "Reset param to initial value",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 1,
												"isparam" : 0,
												"type" : "bang"
											}
,
											"normalized" : 											{
												"attrOrProp" : 1,
												"digest" : "Normalized parameter value.",
												"isalias" : 0,
												"aliases" : [  ],
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"name" : 											{
												"attrOrProp" : 2,
												"digest" : "Parameter Name",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"id" : 											{
												"attrOrProp" : 2,
												"digest" : "DEPRECATED",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"enum" : 											{
												"attrOrProp" : 2,
												"digest" : "Enum Values",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"minimum" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter mininum",
												"isalias" : 0,
												"aliases" : [ "min" ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"min" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter mininum",
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"maximum" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter maximum",
												"isalias" : 0,
												"aliases" : [ "max" ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"max" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter maximum",
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"exponent" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter exponent",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}
,
											"steps" : 											{
												"attrOrProp" : 2,
												"digest" : "parameter steps",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"displayname" : 											{
												"attrOrProp" : 2,
												"digest" : "Display Name",
												"isalias" : 0,
												"aliases" : [ "displayName" ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "symbol",
												"defaultValue" : ""
											}
,
											"displayName" : 											{
												"attrOrProp" : 2,
												"digest" : "Display Name",
												"isalias" : 1,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "symbol",
												"defaultValue" : ""
											}
,
											"unit" : 											{
												"attrOrProp" : 2,
												"digest" : "Unit",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "symbol",
												"defaultValue" : ""
											}
,
											"rect" : 											{
												"attrOrProp" : 2,
												"digest" : "layout rect",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "list",
												"defaultValue" : ""
											}
,
											"style" : 											{
												"attrOrProp" : 2,
												"digest" : "box style",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"enum" : [ "none", "button", "dial", "number", "slider", "toggle" ],
												"type" : "enum",
												"defaultValue" : "slider"
											}
,
											"tonormalized" : 											{
												"attrOrProp" : 2,
												"digest" : "Custom Scaling - convert to normalized (0..1) form.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"fromnormalized" : 											{
												"attrOrProp" : 2,
												"digest" : "Custom Scaling - convert from normalized (0..1) form.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}
,
											"order" : 											{
												"attrOrProp" : 2,
												"digest" : "Restore order.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"noinit" : 											{
												"attrOrProp" : 2,
												"digest" : "Do not send initial value.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"nopreset" : 											{
												"attrOrProp" : 2,
												"digest" : "Do not add this value to the preset [DEPRECATED - USE @preset 0 instead].",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "0"
											}
,
											"preset" : 											{
												"attrOrProp" : 2,
												"digest" : "Add this value to the preset.",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"defaultValue" : "1"
											}

										}
,
										"inputs" : [ 											{
												"name" : "value",
												"type" : "number",
												"digest" : "Parameter value",
												"defaultarg" : 2,
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [ 											{
												"name" : "value",
												"type" : "number",
												"digest" : "Parameter value",
												"defaultarg" : 2,
												"hot" : 1,
												"docked" : 0
											}
, 											{
												"name" : "normalized",
												"type" : "number",
												"digest" : "Normalized parameter value.",
												"docked" : 0
											}
 ],
										"helpname" : "param",
										"aliasOf" : "param",
										"classname" : "param",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : -579982699
									}
,
									"text" : "param freq 497 @minimum 20 @maximum 2000",
									"varname" : "freq"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-1",
									"maxclass" : "newobj",
									"numinlets" : 0,
									"numoutlets" : 1,
									"outlettype" : [ "" ],
									"patching_rect" : [ 467.0, 76.0, 130.0, 23.0 ],
									"rnbo_serial" : 1,
									"rnbo_uniqueid" : "inport_obj-1",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 0,
										"argnames" : 										{
											"tag" : 											{
												"attrOrProp" : 2,
												"digest" : "Tag to receive messages for.",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}

										}
,
										"inputs" : [  ],
										"outputs" : [ 											{
												"name" : "out",
												"type" : [ "bang", "number", "list" ],
												"digest" : "Received Message. ",
												"docked" : 0
											}
 ],
										"helpname" : "inport",
										"classname" : "inport",
										"digest" : "inport",
										"versionId" : 0,
										"expressive" : 0
									}
,
									"text" : "inport /tagged/like/osc"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-5",
									"maxclass" : "newobj",
									"numinlets" : 1,
									"numoutlets" : 0,
									"patching_rect" : [ 104.0, 322.0, 43.0, 23.0 ],
									"rnbo_serial" : 3,
									"rnbo_uniqueid" : "signaloutlet_obj-5",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 1,
										"argnames" : 										{
											"in1" : 											{
												"attrOrProp" : 1,
												"digest" : "signal sent to outlet with index 1",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 0,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "signal"
											}
,
											"index" : 											{
												"attrOrProp" : 2,
												"digest" : "outlet number",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}
,
											"comment" : 											{
												"attrOrProp" : 2,
												"digest" : "mouse over comment",
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number"
											}

										}
,
										"inputs" : [ 											{
												"name" : "in1",
												"type" : "signal",
												"digest" : "signal sent to outlet with index 1",
												"displayName" : "",
												"hot" : 1,
												"docked" : 0
											}
 ],
										"outputs" : [  ],
										"helpname" : "out~",
										"aliasOf" : "out~",
										"classname" : "signaloutlet",
										"expressive" : 0,
										"operator" : 0,
										"versionId" : 0
									}
,
									"text" : "out~ 1"
								}

							}
, 							{
								"box" : 								{
									"id" : "obj-3",
									"maxclass" : "newobj",
									"numinlets" : 0,
									"numoutlets" : 1,
									"outlettype" : [ "" ],
									"patching_rect" : [ 396.0, 76.0, 61.0, 23.0 ],
									"rnbo_serial" : 2,
									"rnbo_uniqueid" : "inport_obj-3",
									"rnboinfo" : 									{
										"needsInstanceInfo" : 0,
										"argnames" : 										{
											"tag" : 											{
												"attrOrProp" : 2,
												"digest" : "Tag to receive messages for.",
												"defaultarg" : 1,
												"isalias" : 0,
												"aliases" : [  ],
												"settable" : 1,
												"attachable" : 0,
												"isparam" : 0,
												"type" : "number",
												"mandatory" : 1
											}

										}
,
										"inputs" : [  ],
										"outputs" : [ 											{
												"name" : "out",
												"type" : [ "bang", "number", "list" ],
												"digest" : "Received Message. ",
												"docked" : 0
											}
 ],
										"helpname" : "inport",
										"classname" : "inport",
										"digest" : "inport",
										"versionId" : 0,
										"expressive" : 0
									}
,
									"text" : "inport foo"
								}

							}
 ],
						"lines" : [ 							{
								"patchline" : 								{
									"destination" : [ "obj-11", 0 ],
									"order" : 1,
									"source" : [ "obj-1", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-9", 0 ],
									"order" : 0,
									"source" : [ "obj-1", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-13", 0 ],
									"source" : [ "obj-12", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-14", 0 ],
									"source" : [ "obj-13", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-15", 0 ],
									"source" : [ "obj-14", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-18", 0 ],
									"source" : [ "obj-17", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-19", 0 ],
									"source" : [ "obj-23", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-10", 0 ],
									"order" : 2,
									"source" : [ "obj-3", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-23", 0 ],
									"order" : 1,
									"source" : [ "obj-3", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-9", 0 ],
									"order" : 0,
									"source" : [ "obj-3", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-8", 0 ],
									"source" : [ "obj-7", 0 ]
								}

							}
, 							{
								"patchline" : 								{
									"destination" : [ "obj-5", 0 ],
									"source" : [ "obj-8", 0 ]
								}

							}
 ],
						"default_bgcolor" : [ 0.031372549019608, 0.125490196078431, 0.211764705882353, 1.0 ],
						"color" : [ 0.929412, 0.929412, 0.352941, 1.0 ],
						"elementcolor" : [ 0.357540726661682, 0.515565991401672, 0.861786782741547, 1.0 ],
						"accentcolor" : [ 0.343034118413925, 0.506230533123016, 0.86220508813858, 1.0 ],
						"stripecolor" : [ 0.258338063955307, 0.352425158023834, 0.511919498443604, 1.0 ],
						"bgfillcolor_type" : "color",
						"bgfillcolor_color" : [ 0.031372549019608, 0.125490196078431, 0.211764705882353, 1.0 ],
						"bgfillcolor_color1" : [ 0.031372549019608, 0.125490196078431, 0.211764705882353, 1.0 ],
						"bgfillcolor_color2" : [ 0.263682, 0.004541, 0.038797, 1.0 ],
						"bgfillcolor_angle" : 270.0,
						"bgfillcolor_proportion" : 0.39,
						"bgfillcolor_autogradient" : 0.0
					}
,
					"patching_rect" : [ 73.0, 124.0, 186.0, 22.0 ],
					"rnboattrcache" : 					{
						"mode" : 						{
							"label" : "mode",
							"isEnum" : 1,
							"parsestring" : "x y z"
						}

					}
,
					"saved_attribute_attributes" : 					{
						"valueof" : 						{
							"parameter_invisible" : 1,
							"parameter_longname" : "rnbo~",
							"parameter_shortname" : "rnbo~",
							"parameter_type" : 3
						}

					}
,
					"saved_object_attributes" : 					{
						"parameter_enable" : 1,
						"uuid" : "60991ad1-9304-11eb-9ad1-acde48001122"
					}
,
					"signalinletcount" : 0,
					"signaloutletcount" : 3,
					"snapshot" : 					{
						"filetype" : "C74Snapshot",
						"version" : 2,
						"minorversion" : 0,
						"name" : "snapshotlist",
						"origin" : "rnbo~",
						"type" : "list",
						"subtype" : "Undefined",
						"embed" : 1,
						"snapshot" : 						{
							"mode" : 							{
								"value" : 0.0
							}
,
							"freq" : 							{
								"value" : 497.0
							}
,
							"__presetid" : "60991ad1-9304-11eb-9ad1-acde48001122"
						}
,
						"snapshotlist" : 						{
							"current_snapshot" : 0,
							"entries" : [ 								{
									"filetype" : "C74Snapshot",
									"version" : 2,
									"minorversion" : 0,
									"name" : "",
									"origin" : "60991ad1-9304-11eb-9ad1-acde48001122",
									"type" : "rnbo",
									"subtype" : "",
									"embed" : 0,
									"snapshot" : 									{
										"mode" : 										{
											"value" : 0.0
										}
,
										"freq" : 										{
											"value" : 497.0
										}
,
										"__presetid" : "60991ad1-9304-11eb-9ad1-acde48001122"
									}
,
									"fileref" : 									{
										"name" : "",
										"filename" : "_20210401.maxsnap",
										"filepath" : "~/Documents/Max 8/Snapshots",
										"filepos" : -1,
										"snapshotfileid" : "1767e2a8d39165d6d4e95f75a4def75a"
									}

								}
 ]
						}

					}
,
					"text" : "rnbo~",
					"varname" : "rnbo~"
				}

			}
 ],
		"lines" : [ 			{
				"patchline" : 				{
					"destination" : [ "obj-2", 1 ],
					"order" : 0,
					"source" : [ "obj-1", 0 ]
				}

			}
, 			{
				"patchline" : 				{
					"destination" : [ "obj-2", 0 ],
					"order" : 1,
					"source" : [ "obj-1", 0 ]
				}

			}
 ],
		"parameters" : 		{
			"obj-1" : [ "rnbo~", "rnbo~", 0 ],
			"parameterbanks" : 			{

			}
,
			"inherited_shortname" : 1
		}
,
		"dependency_cache" : [ 			{
				"name" : "_20210401.maxsnap",
				"bootpath" : "~/Documents/Max 8/Snapshots",
				"patcherrelativepath" : "../../../../../Documents/Max 8/Snapshots",
				"type" : "mx@s",
				"implicit" : 1
			}
, 			{
				"name" : "rnbo~.mxo",
				"type" : "iLaX"
			}
 ],
		"autosave" : 0
	}

}
