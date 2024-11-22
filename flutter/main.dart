// ignore_for_file: avoid_print, use_build_context_synchronously

/* //////////////////////////////////////////////////////////////
                            IMPORTS
////////////////////////////////////////////////////////////// */
import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

/* //////////////////////////////////////////////////////////////
                      MAIN APP CONFIGURATION
////////////////////////////////////////////////////////////// */
void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Smart Intercom',
      theme: ThemeData(
        primarySwatch: Colors.blue,
        brightness: Brightness.dark,
      ),
      home: const IntercomScreen(),
    );
  }
}

/* //////////////////////////////////////////////////////////////
                      INTERCOM SCREEN
////////////////////////////////////////////////////////////// */
class IntercomScreen extends StatefulWidget {
  const IntercomScreen({super.key});

  @override
  State<IntercomScreen> createState() => _IntercomScreenState();
}

class _IntercomScreenState extends State<IntercomScreen> {
  /* STATE VARIABLES */
  final String streamUrl = 'http://192.168.0.102:80';
  final String notificationUrl = 'http://192.168.0.102:81/notification';
  final String unlockUrl = 'http://192.168.0.102:82/unlock';
  StreamController<Uint8List>? _videoStreamController;
  StreamSubscription? _subscription;
  bool _isConnected = false;

  @override
  void initState() {
    super.initState();
    _videoStreamController = StreamController<Uint8List>.broadcast();
    _connectToVideoStream();
    _startNotificationListener();
  }

  /* CAMERA */
  Future<void> _connectToVideoStream() async {
    try {
      final response = await http.Client().send(http.Request('GET', Uri.parse(streamUrl)));

      setState(() => _isConnected = true);

      // Convert response stream to Uint8List for processing
      final Stream<Uint8List> stream = response.stream.cast<Uint8List>();

      // Prepare variables for parsing multipart MJPEG stream
      List<int> imageData = [];
      List<int> boundary = '\r\n--123456789000000000000987654321\r\n'.codeUnits;
      int boundaryIndex = 0;

      // Listen to incoming stream data and process image frames
      _subscription = stream.listen((data) {
        for (int byte in data) {
          imageData.add(byte);
          // Detect stream boundary to separate image frames
          if (byte == boundary[boundaryIndex]) {
            boundaryIndex++;
            if (boundaryIndex == boundary.length) {
              if (imageData.length > boundary.length) {
                _processImageData(imageData.sublist(0, imageData.length - boundary.length));
              }
              imageData = [];
              boundaryIndex = 0;
            }
          } else {
            boundaryIndex = 0;
          }
        }
      },
      onError: (error) {
        setState(() => _isConnected = false);
        _reconnect();
      },
      cancelOnError: true);
    } catch(e) {
      setState(() => _isConnected = false);
      _reconnect();
    }
  }

  void _processImageData(List<int> imageData) {
    try {
       // Find start of JPEG image (JPEG header: 0xFF 0xD8)
      int start = 0;
      for (int i = 0; i < imageData.length - 1; i++) {
        if (imageData[i] == 0xFF && imageData[i + 1] == 0xD8) {
          start = i;
          break;
        }
      }

      // Find end of JPEG image (JPEG footer: 0xFF 0xD9)
      int end = imageData.length;
      for (int i = imageData.length - 2; i >= 0; i--) {
        if (imageData[i] == 0xFF && imageData[i + 1] == 0xD9) {
          end = i + 2;
          break;
        }
      }

      // Extract and stream complete JPEG frame
      if (end > start) {
        final jpegData = Uint8List.fromList(imageData.sublist(start, end));
        _videoStreamController?.add(jpegData);
      }
    } catch (e) {
      print('Error processing image data: $e');
    }
  }

  /* NOTIFICATIONS */
  Future<void> _startNotificationListener() async {
    Timer.periodic(const Duration(seconds: 3), (timer) async {
      try {
        final response = await http.get(Uri.parse(notificationUrl));
        
        // Check if request was successful
        if (response.statusCode == 200) {
          // Parse JSON response
          final jsonResponse = json.decode(response.body);
          if (jsonResponse['event'] == 'switch_pressed') {
            _showNotificationDialog('Someone is at the door');
          }
        }
      } catch (e) {
        print('Notification check error: $e');
      }
    });
  }

  void _showNotificationDialog(String message) {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Switch Activated'),
        content: Text(message),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  /* UNLOCKING */
  Future<void> _sendUnlockCommand() async {
    try {
      final response = await http.get(Uri.parse(unlockUrl));

      // Check if request was successful
      if (response.statusCode == 200) {
        // Notify that door is unlocked
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Door unlocked')),
        );
      }
    } catch (e) {
      print('Unlock command error: $e');
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Failed to unlock door')),
      );
    }
  }

  void _reconnect() {
    Future.delayed(const Duration(seconds: 5), () {
      if (!_isConnected && mounted) {
        _connectToVideoStream();
      }
    });
  }

  @override
  void dispose() {
    _subscription?.cancel();
    _videoStreamController?.close();
    super.dispose();
  }

/* //////////////////////////////////////////////////////////////
                      MAIN INTERFACE DESIGN
////////////////////////////////////////////////////////////// */
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Smart Intercom'),
        actions: [
          Container(
            margin: const EdgeInsets.all(16.0),
            child: Row(
              children: [
                Icon(
                  _isConnected ? Icons.circle : Icons.error,
                  color: _isConnected ? const Color.fromARGB(255, 46, 167, 50) : const Color.fromARGB(255, 201, 45, 34),
                ),
                const SizedBox(width: 8),
                Text(_isConnected ? 'Connected' : 'Disconnected'),
              ],
            ),
          ),
        ],
      ),
      body: Row(
        children: [
          // Video Stream Panel
          Expanded(
            flex: 3,
            child: Container(
              margin: const EdgeInsets.all(16.0),
              decoration: BoxDecoration(
                color: Colors.black,
                borderRadius: BorderRadius.circular(8.0),
              ),
              // Dynamically updates UI based on a video stream
              child: StreamBuilder<Uint8List>(
                stream: _videoStreamController?.stream,
                builder: (context,snapshot) {
                  if (!_isConnected) {
                    return const Center(
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Icon(Icons.error_outline, size: 48, color: Color.fromARGB(255, 201, 45, 34)),
                          SizedBox(height: 16),
                          Text('Connection lost. Reconnecting...'),
                        ],
                      ),
                    );
                  }
                  // Check if no video data is available
                  if (!snapshot.hasData) {
                    return const Center(child: CircularProgressIndicator());
                  }

                  // Display video frame when data is available
                  return Image.memory(
                    snapshot.data!, // Decodes the Uint8List to display as an image
                    gaplessPlayback: true, // Smooth transition between frames
                    fit: BoxFit.contain,
                  );
                },
              ),
            ),
          ),
          // Control Panel
          Expanded(
            flex: 1,
            child: Container(
              margin: const EdgeInsets.all(16.0),
              decoration: BoxDecoration(
                color: Colors.grey[850],
                borderRadius: BorderRadius.circular(8.0),
              ),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  ElevatedButton.icon(
                    icon: const Icon(Icons.door_front_door_outlined), 
                    label: const Text('Open Door'),
                    style: ElevatedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 32.0,
                        vertical: 16.0,
                      ),
                    ),
                    onPressed: _sendUnlockCommand,
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}