import 'package:go_router/go_router.dart';
import 'package:basic_ble_app/screens/home.dart';

final appRouter = GoRouter(
  initialLocation: "/home",
  
  routes: [
  GoRoute(path: "/home", builder: (context, state) => HomeScreen()),
]
 
);