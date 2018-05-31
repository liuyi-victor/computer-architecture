int main()
{
	int i, a = 0;
	for(i=0; i < 1000000; i++)
	{
		if(i % 5 == 0)
		{
			a = a-1;
		}
		a = a+1;
		if(i % 8 == 0)
			a = a+1;
		/*
		object dump of the loop body
		address				instruction
			
		 672:	eb 40                	jmp    6b4 <main+0x54>
		 674:	8b 4d fc             	mov    -0x4(%rbp),%ecx
		 677:	ba 67 66 66 66       	mov    $0x66666667,%edx
		 67c:	89 c8                	mov    %ecx,%eax
		 67e:	f7 ea                	imul   %edx
		 680:	d1 fa                	sar    %edx
		 682:	89 c8                	mov    %ecx,%eax
		 684:	c1 f8 1f             	sar    $0x1f,%eax
		 687:	29 c2                	sub    %eax,%edx
		 689:	89 d0                	mov    %edx,%eax
		 68b:	89 c2                	mov    %eax,%edx
		 68d:	c1 e2 02             	shl    $0x2,%edx
		 690:	01 c2                	add    %eax,%edx
		 692:	89 c8                	mov    %ecx,%eax
		 694:	29 d0                	sub    %edx,%eax
		 696:	85 c0                	test   %eax,%eax
		 698:	75 04                	jne    69e <main+0x3e>
		 69a:	83 6d f8 01          	subl   $0x1,-0x8(%rbp)
		 69e:	83 45 f8 01          	addl   $0x1,-0x8(%rbp)
		 6a2:	8b 45 fc             	mov    -0x4(%rbp),%eax
		 6a5:	83 e0 07             	and    $0x7,%eax
		 6a8:	85 c0                	test   %eax,%eax
		 6aa:	75 04                	jne    6b0 <main+0x50>
		 6ac:	83 45 f8 01          	addl   $0x1,-0x8(%rbp)
		 6b0:	83 45 fc 01          	addl   $0x1,-0x4(%rbp)
		 6b4:	81 7d fc 9f 86 01 00 	cmpl   $0x1869f,-0x4(%rbp)
		 6bb:	7e b7                	jle    674 <main+0x14>
		*/
	}
}
